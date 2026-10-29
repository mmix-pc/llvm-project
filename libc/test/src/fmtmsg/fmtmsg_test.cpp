//===-- Production fmtmsg with controlled output destinations -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/file.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/write.h"
#include "src/fmtmsg/fmtmsg.h"
#include "src/stdio/stderr.h"
#include "src/stdlib/getenv.h"
#include "test/UnitTest/Test.h"
#include <fmtmsg.h>
#include <initializer_list>
#include <pthread.h>

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;
using LIBC_NAMESPACE::cpp::string_view;

namespace {
struct Sink {
  char bytes[32768] = {};
  size_t size = 0;
  size_t limit = sizeof(bytes) - 1;
  size_t chunk = sizeof(bytes) - 1;
  int error = EIO;
  unsigned calls = 0;

  FileIOResult write(const void *data, size_t count) {
    ++calls;
    if (size == limit)
      return {0, error};
    size_t n = count < chunk ? count : chunk;
    if (n > limit - size)
      n = limit - size;
    if (n > sizeof(bytes) - 1 - size) {
      EXPECT_LE(n, sizeof(bytes) - 1 - size);
      return {0, EIO};
    }
    for (size_t i = 0; i < n; ++i)
      bytes[size++] = static_cast<const char *>(data)[i];
    bytes[size] = '\0';
    return n;
  }

  unsigned occurrences(string_view needle) const {
    unsigned count = 0;
    string_view output(bytes, size);
    for (size_t i = 0; i + needle.size() <= size; ++i)
      if (output.substr(i, needle.size()) == needle)
        ++count;
    return count;
  }
};

struct LockProbe {
  File *file;
  bool acquired = false;
};

void *try_lock(void *arg) {
  auto &probe = *static_cast<LockProbe *>(arg);
  probe.acquired = probe.file->try_lock();
  if (probe.acquired)
    probe.file->unlock();
  return nullptr;
}

void check_lock(File &file, bool available) {
  LockProbe probe{&file};
  pthread_t thread;
  ASSERT_EQ(::pthread_create(&thread, nullptr, try_lock, &probe), 0);
  ASSERT_EQ(::pthread_join(thread, nullptr), 0);
  EXPECT_EQ(probe.acquired, available);
}

class CapturedFile : public File {
  static FileIOResult write_output(File *base, const void *data, size_t size) {
    auto &self = *static_cast<CapturedFile *>(base);
    if (self.probe_lock)
      check_lock(self, false);
    return self.sink.write(data, size);
  }

public:
  Sink sink;
  bool probe_lock = false;

  CapturedFile()
      : File(write_output, nullptr, nullptr, nullptr, nullptr, 0, _IONBF, false,
             mode_flags("w")) {}
};

struct Console {
  Sink sink;
  int fd = 37;
  int open_error = 0, close_error = 0;
  unsigned opens = 0, closes = 0;
  bool live = false;
};

Console *console;
const char *verbosity;
unsigned environment_reads;
} // namespace

namespace LIBC_NAMESPACE_DECL {
FILE *fmtmsg_test_stderr;

char *fmtmsg_test_getenv(const char *name) {
  EXPECT_STREQ(name, "MSGVERB");
  ++environment_reads;
  return const_cast<char *>(verbosity);
}

namespace fmtmsg_test_syscalls {
ErrorOr<int> open(const char *path, int flags, mode_t mode) {
  EXPECT_STREQ(path, "/dev/console");
  EXPECT_EQ(flags, O_WRONLY | O_CLOEXEC | O_NOCTTY);
  EXPECT_EQ(mode, mode_t(0));
  EXPECT_FALSE(console->live);
  ++console->opens;
  if (console->open_error)
    return Error(console->open_error);
  console->live = true;
  return console->fd;
}

ErrorOr<ssize_t> write(int fd, const void *data, size_t count) {
  EXPECT_EQ(fd, console->fd);
  EXPECT_TRUE(console->live);
  auto result = console->sink.write(data, count);
  if (result.has_error())
    return Error(result.error);
  return static_cast<ssize_t>(result.value);
}

ErrorOr<int> close(int fd) {
  EXPECT_EQ(fd, console->fd);
  EXPECT_TRUE(console->live);
  console->live = false;
  ++console->closes;
  if (console->close_error)
    return Error(console->close_error);
  return 0;
}
} // namespace fmtmsg_test_syscalls
} // namespace LIBC_NAMESPACE_DECL

// Include the actual public provider. Only external inputs and console syscalls
// are replaced; parsing, formatting, File buffering/locking and return mapping
// run unchanged. No test can open or write the host console.
#pragma push_macro("stderr")
#undef stderr
#define stderr fmtmsg_test_stderr
#define getenv fmtmsg_test_getenv
#define linux_syscalls fmtmsg_test_syscalls
#include "src/fmtmsg/fmtmsg.cpp"
#undef linux_syscalls
#undef getenv
#pragma pop_macro("stderr")

static_assert(!noexcept(LIBC_NAMESPACE::fmtmsg(MM_PRINT, MM_NULLLBL, MM_NOSEV,
                                               MM_NULLTXT, MM_NULLACT,
                                               MM_NULLTAG)));

namespace {
class Fixture {
public:
  CapturedFile err;
  Console con;

  Fixture() {
    LIBC_NAMESPACE::fmtmsg_test_stderr = reinterpret_cast<FILE *>(&err);
    console = &con;
    verbosity = nullptr;
    environment_reads = 0;
  }

  ~Fixture() {
    EXPECT_FALSE(con.live);
    LIBC_NAMESPACE::fmtmsg_test_stderr = nullptr;
    console = nullptr;
  }

  int emit(long classification = MM_PRINT | MM_CONSOLE) {
    return LIBC_NAMESPACE::fmtmsg(classification, "source:utility", MM_WARNING,
                                  "condition", "repair", "manual:42");
  }
};

const char *const tokens[] = {"source:utility", "WARNING", "condition",
                              "repair", "manual:42"};

void check_components(const Sink &sink, unsigned mask) {
  for (unsigned i = 0; i < 5; ++i)
    EXPECT_EQ(sink.occurrences(tokens[i]), (mask >> i) & 1U);
  EXPECT_EQ(sink.occurrences("TO FIX:"), (mask >> 3) & 1U);
}
} // namespace

TEST(LlvmLibcFmtmsgTest, DestinationsAndClassifications) {
  const long destinations[] = {MM_NULLMC, MM_PRINT, MM_CONSOLE,
                               MM_PRINT | MM_CONSOLE};
  for (long destination : destinations) {
    Fixture f;
    EXPECT_EQ(f.emit(destination | MM_SOFT | MM_UTIL | MM_RECOVER), int(MM_OK));
    check_components(f.err.sink, destination & MM_PRINT ? 31U : 0U);
    check_components(f.con.sink, destination & MM_CONSOLE ? 31U : 0U);
    EXPECT_EQ(environment_reads, destination & MM_PRINT ? 1U : 0U);
    EXPECT_EQ(f.con.opens, destination & MM_CONSOLE ? 1U : 0U);
    EXPECT_EQ(f.con.closes, f.con.opens);
  }
  Fixture f;
  EXPECT_EQ(f.emit(MM_NULLMC), int(MM_OK));
  EXPECT_EQ(f.err.sink.calls, 0U);
  EXPECT_EQ(f.con.opens, 0U);
}

TEST(LlvmLibcFmtmsgTest, StandardSeveritiesAndLabelBounds) {
  const int severities[] = {MM_NOSEV, MM_HALT, MM_ERROR, MM_WARNING, MM_INFO};
  const char *const names[] = {"HALT", "ERROR", "WARNING", "INFO"};
  const char *const labels[] = {MM_NULLLBL, ":", "x:", ":utility",
                                "0123456789:01234567890123"};
  for (int severity : severities) {
    for (const char *label : labels) {
      Fixture f;
      EXPECT_EQ(LIBC_NAMESPACE::fmtmsg(MM_PRINT | MM_CONSOLE, label, severity,
                                       "condition", MM_NULLACT, "manual:42"),
                int(MM_OK));
      for (const Sink *sink : {&f.err.sink, &f.con.sink}) {
        for (unsigned i = 0; i < 4; ++i)
          EXPECT_EQ(sink->occurrences(names[i]),
                    severity == severities[i + 1] ? 1U : 0U);
        if (label)
          EXPECT_GT(sink->occurrences(label), 0U);
        EXPECT_EQ(sink->occurrences("manual:42"), 1U);
        EXPECT_EQ(sink->occurrences("TO FIX:"), 0U);
      }
    }
  }
}

TEST(LlvmLibcFmtmsgTest, EveryNullComponentCombination) {
  for (unsigned mask = 0; mask < 32; ++mask) {
    Fixture f;
    EXPECT_EQ(LIBC_NAMESPACE::fmtmsg(MM_PRINT | MM_CONSOLE,
                                     mask & 1 ? tokens[0] : MM_NULLLBL,
                                     mask & 2 ? MM_WARNING : MM_NOSEV,
                                     mask & 4 ? tokens[2] : MM_NULLTXT,
                                     mask & 8 ? tokens[3] : MM_NULLACT,
                                     mask & 16 ? tokens[4] : MM_NULLTAG),
              int(MM_OK));
    check_components(f.err.sink, mask);
    check_components(f.con.sink, mask);
  }
}

TEST(LlvmLibcFmtmsgTest, EveryVerbositySubsetInReverseOrder) {
  const string_view keywords[] = {"label", "severity", "text", "action", "tag"};
  for (unsigned mask = 1; mask < 32; ++mask) {
    Fixture f;
    char value[64];
    size_t length = 0;
    for (int i = 4; i >= 0; --i) {
      if (!(mask & (1U << i)))
        continue;
      if (length)
        value[length++] = ':';
      for (char c : keywords[i])
        value[length++] = c;
    }
    value[length] = '\0';
    verbosity = value;
    EXPECT_EQ(f.emit(), int(MM_OK));
    check_components(f.err.sink, mask);
    check_components(f.con.sink, 31);
  }
}

TEST(LlvmLibcFmtmsgTest, InvalidVerbositySelectsAll) {
  const char *const values[] = {nullptr,
                                "",
                                ":",
                                ":text",
                                "text:",
                                "text::tag",
                                "text:unknown",
                                "unknown:text",
                                "TEXT",
                                " text",
                                "text ",
                                "text,tag",
                                "severity=INFO"};
  for (const char *value : values) {
    Fixture f;
    verbosity = value;
    EXPECT_EQ(f.emit(), int(MM_OK));
    check_components(f.err.sink, 31);
    check_components(f.con.sink, 31);
  }
}

TEST(LlvmLibcFmtmsgTest, DuplicateKeywordsSelectOnceAndChangesAreObserved) {
  Fixture f;
  verbosity = "tag:text:tag:text";
  EXPECT_EQ(f.emit(), int(MM_OK));
  check_components(f.err.sink, 20);
  check_components(f.con.sink, 31);
  f.err.sink = {};
  f.con.sink = {};
  verbosity = "severity:severity:action";
  EXPECT_EQ(f.emit(), int(MM_OK));
  check_components(f.err.sink, 10);
  check_components(f.con.sink, 31);
  f.err.sink = {};
  f.con.sink = {};
  verbosity = nullptr;
  EXPECT_EQ(f.emit(), int(MM_OK));
  check_components(f.err.sink, 31);
  EXPECT_EQ(environment_reads, 3U);
}

TEST(LlvmLibcFmtmsgTest, SelectedNullComponentDoesNotSuppressConsole) {
  Fixture f;
  verbosity = "label";
  EXPECT_EQ(LIBC_NAMESPACE::fmtmsg(MM_PRINT | MM_CONSOLE, MM_NULLLBL,
                                   MM_WARNING, tokens[2], tokens[3], tokens[4]),
            int(MM_OK));
  EXPECT_EQ(f.err.sink.size, size_t(0));
  check_components(f.con.sink, 30);
}

TEST(LlvmLibcFmtmsgTest, StderrRetainsItsBufferingAndPendingOutput) {
  Fixture f;
  char buffer[256];
  ASSERT_EQ(f.err.set_buffer(buffer, sizeof(buffer), _IOFBF), 0);
  ASSERT_EQ(f.err.write("pending ", 8).value, size_t(8));
  EXPECT_EQ(f.emit(), int(MM_OK));
  EXPECT_EQ(f.err.sink.calls, 0U);
  check_components(f.con.sink, 31);
  ASSERT_EQ(f.err.flush(), 0);
  EXPECT_EQ(f.err.sink.occurrences("pending "), 1U);
  check_components(f.err.sink, 31);
}

TEST(LlvmLibcFmtmsgTest, LongTextAndActionAndShortWrites) {
  Fixture f;
  char text[4097], action[8193];
  for (size_t i = 0; i < sizeof(text) - 1; ++i)
    text[i] = 't';
  text[sizeof(text) - 1] = '\0';
  for (size_t i = 0; i < sizeof(action) - 1; ++i)
    action[i] = 'a';
  action[sizeof(action) - 1] = '\0';
  f.err.sink.chunk = 7;
  f.con.sink.chunk = 11;
  f.con.fd = 0;
  EXPECT_EQ(LIBC_NAMESPACE::fmtmsg(MM_PRINT | MM_CONSOLE, MM_NULLLBL, MM_NOSEV,
                                   text, action, "manual:42"),
            int(MM_OK));
  for (const Sink *sink : {&f.err.sink, &f.con.sink}) {
    EXPECT_EQ(sink->occurrences(text), 1U);
    EXPECT_EQ(sink->occurrences(action), 1U);
    EXPECT_EQ(sink->occurrences("TO FIX:"), 1U);
    EXPECT_EQ(sink->occurrences("manual:42"), 1U);
  }
  EXPECT_EQ(f.con.closes, 1U);
}

TEST(LlvmLibcFmtmsgTest, IndependentOutputFailures) {
  const long destinations[] = {MM_PRINT, MM_CONSOLE, MM_PRINT | MM_CONSOLE};
  for (long destination : destinations) {
    for (unsigned failures = 0; failures < 4; ++failures) {
      Fixture f;
      if (failures & 1)
        f.err.sink.limit = 0;
      if (failures & 2)
        f.con.sink.limit = 0;
      bool print_failed = (destination & MM_PRINT) && (failures & 1);
      bool console_failed = (destination & MM_CONSOLE) && (failures & 2);
      int expected = print_failed && console_failed ? MM_NOTOK
                     : print_failed                 ? MM_NOMSG
                     : console_failed               ? MM_NOCON
                                                    : MM_OK;
      EXPECT_EQ(f.emit(destination), expected);
      EXPECT_EQ(f.err.error(), print_failed);
      if ((destination & MM_PRINT) && !print_failed)
        check_components(f.err.sink, 31);
      if ((destination & MM_CONSOLE) && !console_failed)
        check_components(f.con.sink, 31);
      EXPECT_EQ(f.con.opens, destination & MM_CONSOLE ? 1U : 0U);
      EXPECT_EQ(f.con.closes, f.con.opens);
    }
  }
}

TEST(LlvmLibcFmtmsgTest, ConsoleOpenAndCloseFailures) {
  const int errors[] = {EACCES, ENOENT, EMFILE, EIO, EINTR};
  for (int error : errors) {
    for (bool stderr_fails : {false, true}) {
      for (bool opening : {false, true}) {
        Fixture f;
        if (stderr_fails)
          f.err.sink.limit = 0;
        if (opening)
          f.con.open_error = error;
        else
          f.con.close_error = error;
        EXPECT_EQ(f.emit(), int(stderr_fails ? MM_NOTOK : MM_NOCON));
        EXPECT_EQ(f.con.opens, 1U);
        EXPECT_EQ(f.con.closes, opening ? 0U : 1U);
        if (opening)
          EXPECT_EQ(f.con.sink.calls, 0U);
        else
          check_components(f.con.sink, 31);
        if (!stderr_fails)
          check_components(f.err.sink, 31);
      }
    }
  }
}

TEST(LlvmLibcFmtmsgTest, PartialAndZeroProgressFailures) {
  const size_t prefixes[] = {0, 3, 17, 35};
  const int errors[] = {0, EIO, EINTR, EAGAIN};
  for (size_t prefix : prefixes) {
    for (int error : errors) {
      Fixture f;
      f.err.sink.limit = f.con.sink.limit = prefix;
      f.err.sink.error = f.con.sink.error = error;
      EXPECT_EQ(f.emit(), int(MM_NOTOK));
      EXPECT_EQ(f.err.sink.size, prefix);
      EXPECT_EQ(f.con.sink.size, prefix);
      EXPECT_TRUE(f.err.error());
      EXPECT_EQ(f.con.closes, 1U);
    }
  }
}

TEST(LlvmLibcFmtmsgTest, StreamLockCoversMessageAndIsReleasedOnFailure) {
  for (bool fail : {false, true}) {
    Fixture f;
    f.err.probe_lock = true;
    if (fail)
      f.err.sink.limit = 17;
    EXPECT_EQ(f.emit(), int(fail ? MM_NOMSG : MM_OK));
    EXPECT_GT(f.err.sink.calls, 1U);
    check_lock(f.err, true);
    check_components(f.con.sink, 31);
  }
}

TEST(LlvmLibcFmtmsgTest, ImplementationRejectsInvalidLabelAndSeverity) {
  // Invalid input outcomes are local defensive policy, not POSIX assertions.
  const char *const labels[] = {"missingcolon", "a:b:c", "01234567890:x",
                                "x:012345678901234"};
  for (const char *label : labels) {
    Fixture f;
    EXPECT_EQ(LIBC_NAMESPACE::fmtmsg(MM_PRINT | MM_CONSOLE, label, MM_INFO,
                                     "condition", MM_NULLACT, MM_NULLTAG),
              int(MM_NOTOK));
    EXPECT_EQ(f.err.sink.calls, 0U);
    EXPECT_EQ(f.con.opens, 0U);
  }
  const int severities[] = {-1, 5, 256};
  for (int severity : severities) {
    Fixture f;
    EXPECT_EQ(LIBC_NAMESPACE::fmtmsg(MM_PRINT | MM_CONSOLE, "source:utility",
                                     severity, "condition", MM_NULLACT,
                                     MM_NULLTAG),
              int(MM_NOTOK));
    EXPECT_EQ(f.err.sink.calls, 0U);
    EXPECT_EQ(f.con.opens, 0U);
  }
}
