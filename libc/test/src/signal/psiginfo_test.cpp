//===-- Tests for psiginfo ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/stdio_macros.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/signal/psiginfo.h"
#include "src/stdio/stderr.h"
#include "src/string/strsignal.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;
using LIBC_NAMESPACE::cpp::StringStream;

namespace {
constexpr File::Orientation ORIENTATIONS[] = {File::Orientation::UNORIENTED,
                                              File::Orientation::BYTE,
                                              File::Orientation::WIDE};

class Capture : public File {
  static FileIOResult write_output(File *base, const void *data, size_t count) {
    auto &file = *static_cast<Capture *>(base);
    size_t written = 0;
    while (written < count && file.used < file.fail_after &&
           file.used < sizeof(file.bytes) - 1)
      file.bytes[file.used++] = static_cast<const char *>(data)[written++];
    file.bytes[file.used] = '\0';
    // Even a successful backend write can change errno.
    LIBC_NAMESPACE::libc_errno = EAGAIN;
    return {written, written == count ? 0 : file.failure};
  }

public:
  char bytes[1024]{};
  size_t used = 0;
  size_t fail_after = sizeof(bytes) - 1;
  int failure = ENOSPC;
  explicit Capture(Orientation orientation)
      : File(write_output, nullptr, nullptr, nullptr, nullptr, 0, _IONBF, false,
             mode_flags("w")) {
    try_set_orientation_unlocked(orientation);
  }
};

class StderrGuard {
  ::FILE *saved = LIBC_NAMESPACE::stderr;

public:
  explicit StderrGuard(File &file) {
    LIBC_NAMESPACE::stderr = reinterpret_cast<::FILE *>(&file);
  }
  ~StderrGuard() { LIBC_NAMESPACE::stderr = saved; }
};

void expect_message(const siginfo_t &info, const char *suffix) {
  unsigned char original[sizeof(info)];
  const auto *bytes = reinterpret_cast<const unsigned char *>(&info);
  for (size_t i = 0; i < sizeof(info); ++i)
    original[i] = bytes[i];

  for (auto orientation : ORIENTATIONS) {
    Capture file(orientation);
    StderrGuard guard(file);
    const char *expected = LIBC_NAMESPACE::strsignal(info.si_signo);
    LIBC_NAMESPACE::libc_errno = ERANGE;
    LIBC_NAMESPACE::psiginfo(&info, nullptr);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
    EXPECT_EQ(file.get_orientation(), orientation);
    EXPECT_FALSE(file.error());
    size_t length = 0;
    while (expected[length]) {
      ASSERT_LT(length, file.used);
      EXPECT_EQ(file.bytes[length], expected[length]);
      ++length;
    }
    EXPECT_STREQ(file.bytes + length, suffix);
    for (size_t i = 0; i < sizeof(info); ++i)
      EXPECT_EQ(bytes[i], original[i]);
  }
}

void poison_info(siginfo_t &info, int signo, int code) {
  // Unrelated fields must neither leak into the output nor be modified.
  auto *bytes = reinterpret_cast<unsigned char *>(&info);
  for (size_t i = 0; i < sizeof(info); ++i)
    bytes[i] = 0xa5;
  info.si_signo = signo;
  info.si_code = code;
  info.si_errno = 0;
}
} // namespace

TEST(LlvmLibcPsiginfoTest, PrefixMessageAndOrientation) {
  LocaleGuard locale(false);
  siginfo_t info{};
  info.si_signo = SIGTERM;
  info.si_code = SI_USER;
  const char *prefixes[] = {nullptr, "", "operation"};
  for (auto orientation : ORIENTATIONS) {
    for (const char *prefix : prefixes) {
      Capture file(orientation);
      StderrGuard guard(file);
      LIBC_NAMESPACE::libc_errno = EDOM;
      LIBC_NAMESPACE::psiginfo(&info, prefix);
      EXPECT_STREQ(file.bytes,
                   prefix && *prefix
                       ? "operation: Terminated (sent by kill(), pid=0, uid=0)\n"
                       : "Terminated (sent by kill(), pid=0, uid=0)\n");
      EXPECT_EQ(file.get_orientation(), orientation);
      EXPECT_FALSE(file.error());
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    }
  }
}

TEST(LlvmLibcPsiginfoTest, SignalDescriptionsAndUnchangedInfo) {
  LocaleGuard locale;
  const int signals[] = {SIGINT, SIGTERM, SIGUSR1, SIGCHLD,
                         SIGSEGV, SIGRTMIN, SIGRTMAX};
  for (int signal : signals) {
    siginfo_t info;
    poison_info(info, signal, SI_USER);
    info.si_pid = 123;
    info.si_uid = 456;
    expect_message(info, " (sent by kill(), pid=123, uid=456)\n");
  }
}

TEST(LlvmLibcPsiginfoTest, OriginsSelectOnlyApplicablePayloads) {
  LocaleGuard locale(false);
  const struct {
    int signo;
    int code;
    const char *suffix;
  } cases[] = {
      {SIGSEGV, SI_USER, " (sent by kill(), pid=123, uid=456)\n"},
#ifdef SI_TKILL
      {SIGCHLD, SI_TKILL, " (sent by tkill()/tgkill(), pid=123, uid=456)\n"},
#endif
      {SIGBUS, SI_QUEUE,
       " (sent by sigqueue(), pid=123, uid=456, value(int)=-789)\n"},
      {SIGCHLD, SI_TIMER,
       " (timer expired, timerid=12, overrun=34, value(int)=-789)\n"},
      {SIGPOLL, SI_MESGQ,
       " (message queue notification, pid=123, uid=456, value(int)=-789)\n"},
      {SIGSEGV, SI_ASYNCIO, " (asynchronous I/O completed, value(int)=-789)\n"},
  };
  for (const auto &test : cases) {
    siginfo_t info;
    poison_info(info, test.signo, test.code);
    switch (test.code) {
    case SI_USER:
#ifdef SI_TKILL
    case SI_TKILL:
#endif
    case SI_QUEUE:
    case SI_MESGQ:
      info.si_pid = 123;
      info.si_uid = 456;
      if (test.code == SI_QUEUE || test.code == SI_MESGQ)
        info.si_value.sival_int = -789;
      break;
    case SI_TIMER:
      info.si_timerid = 12;
      info.si_overrun = 34;
      info.si_value.sival_int = -789;
      break;
    case SI_ASYNCIO:
      info.si_value.sival_int = -789;
      break;
    }
    expect_message(info, test.suffix);
  }
}

TEST(LlvmLibcPsiginfoTest, FaultCausesAndAddresses) {
  LocaleGuard locale(false);
  const struct {
    int signo;
    int code;
    const char *cause;
  } cases[] = {
      {SIGILL, ILL_ILLOPC, "illegal opcode"},
      {SIGILL, ILL_ILLOPN, "illegal operand"},
      {SIGILL, ILL_ILLADR, "illegal addressing mode"},
      {SIGILL, ILL_ILLTRP, "illegal trap"},
      {SIGILL, ILL_PRVOPC, "privileged opcode"},
      {SIGILL, ILL_PRVREG, "privileged register"},
      {SIGILL, ILL_COPROC, "coprocessor error"},
      {SIGILL, ILL_BADSTK, "invalid stack"},
      {SIGFPE, FPE_INTDIV, "integer division by zero"},
      {SIGFPE, FPE_INTOVF, "integer overflow"},
      {SIGFPE, FPE_FLTDIV, "floating-point division by zero"},
      {SIGFPE, FPE_FLTOVF, "floating-point overflow"},
      {SIGFPE, FPE_FLTUND, "floating-point underflow"},
      {SIGFPE, FPE_FLTRES, "inexact floating-point result"},
      {SIGFPE, FPE_FLTINV, "invalid floating-point operation"},
      {SIGFPE, FPE_FLTSUB, "subscript out of range"},
      {SIGSEGV, SEGV_MAPERR, "unmapped address"},
      {SIGSEGV, SEGV_ACCERR, "invalid memory permissions"},
      {SIGBUS, BUS_ADRALN, "misaligned address"},
      {SIGBUS, BUS_ADRERR, "nonexistent physical address"},
      {SIGBUS, BUS_OBJERR, "object-specific hardware error"},
      {SIGTRAP, TRAP_BRKPT, "breakpoint"},
      {SIGTRAP, TRAP_TRACE, "trace trap"},
  };
  for (const auto &test : cases) {
    siginfo_t info;
    poison_info(info, test.signo, test.code);
    info.si_addr = reinterpret_cast<void *>(uintptr_t(0x1234));
    char expected[128];
    StringStream out(expected);
    out << " (" << test.cause << ", addr=0x1234)\n" << '\0';
    ASSERT_FALSE(out.overflow());
    expect_message(info, expected);
  }
  siginfo_t info{};
  info.si_signo = SIGSEGV;
  info.si_code = SEGV_MAPERR;
  info.si_addr = nullptr;
  expect_message(info, " (unmapped address, addr=0x0)\n");
}

TEST(LlvmLibcPsiginfoTest, ChildCausesAndStatus) {
  LocaleGuard locale(false);
  const struct {
    int code;
    int status;
    const char *cause;
  } cases[] = {{CLD_EXITED, 42, "child exited"},
               {CLD_KILLED, SIGKILL, "child killed"},
               {CLD_DUMPED, SIGSEGV, "child dumped core"},
               {CLD_TRAPPED, SIGTRAP, "child trapped"},
               {CLD_STOPPED, SIGSTOP, "child stopped"},
               {CLD_CONTINUED, SIGCONT, "child continued"}};
  for (const auto &test : cases) {
    siginfo_t info;
    poison_info(info, SIGCHLD, test.code);
    info.si_pid = 123;
    info.si_uid = 456;
    info.si_status = test.status;
    char expected[128];
    StringStream out(expected);
    out << " (" << test.cause << ", pid=123, uid=456, status=" << test.status
        << ")\n" << '\0';
    ASSERT_FALSE(out.overflow());
    expect_message(info, expected);
  }
}

TEST(LlvmLibcPsiginfoTest, PollCausesAndApplicableBand) {
  LocaleGuard locale(false);
  const struct {
    int code;
    const char *suffix;
  } cases[] = {{POLL_IN, " (input available, band=-17)\n"},
               {POLL_OUT, " (output available, band=-17)\n"},
               {POLL_MSG, " (input message available, band=-17)\n"},
               {POLL_ERR, " (I/O error)\n"},
               {POLL_PRI, " (priority input available)\n"},
               {POLL_HUP, " (device disconnected)\n"},
#ifdef SI_SIGIO
               {SI_SIGIO, " (queued I/O notification, band=-17)\n"},
#endif
  };
  for (const auto &test : cases) {
    siginfo_t info;
    poison_info(info, SIGPOLL, test.code);
    info.si_band = -17;
    expect_message(info, test.suffix);
  }
}

TEST(LlvmLibcPsiginfoTest, OriginsWithoutApplicablePayload) {
  LocaleGuard locale(false);
  siginfo_t info;
#ifdef SI_KERNEL
  poison_info(info, SIGSEGV, SI_KERNEL);
  expect_message(info, " (sent by kernel)\n");
#endif
#ifdef SI_ASYNCNL
  poison_info(info, SIGCHLD, SI_ASYNCNL);
  expect_message(info, " (asynchronous name lookup completed)\n");
#endif
}

TEST(LlvmLibcPsiginfoTest, UnknownCodesDoNotExposePayload) {
  LocaleGuard locale(false);
  const struct {
    int code;
    const char *suffix;
  } cases[] = {{-1234, " (code=-1234)\n"},
               {1234, " (code=1234)\n"},
               {INT_MIN, " (code=-2147483648)\n"},
               {INT_MAX, " (code=2147483647)\n"}};
  const int signals[] = {SIGSEGV, SIGCHLD, SIGPOLL, SIGUSR1};
  for (int signal : signals) {
    for (const auto &test : cases) {
      siginfo_t info;
      poison_info(info, signal, test.code);
      expect_message(info, test.suffix);
    }
  }
  // A code recognized for another signal must not select that signal's fields.
  siginfo_t info;
  poison_info(info, SIGTERM, ILL_ILLOPC);
  expect_message(info, " (code=1)\n");
}

TEST(LlvmLibcPsiginfoTest, UnknownSignalsDoNotExposePayload) {
  LocaleGuard locale(false);
  const struct {
    int signo;
    int code;
  } cases[] = {{0, SI_USER},
               {-1234, SI_QUEUE},
               {SIGRTMAX + 1, SI_TIMER},
               {INT_MIN, SI_ASYNCIO},
               {INT_MAX, SEGV_MAPERR}};
  for (const auto &test : cases) {
    siginfo_t info;
    poison_info(info, test.signo, test.code);
    char expected[64];
    StringStream out(expected);
    out << " (code=" << test.code << ")\n" << '\0';
    ASSERT_FALSE(out.overflow());
    expect_message(info, expected);
  }
}

TEST(LlvmLibcPsiginfoTest, UnknownCauseDoesNotRequireInitializedPayload) {
  LocaleGuard locale(false);
  // Leave the union uninitialized so sanitizer runs can detect stray reads.
  siginfo_t info;
  info.si_signo = SIGSEGV;
  info.si_code = 1234;
  info.si_errno = 0;
  Capture file(File::Orientation::UNORIENTED);
  StderrGuard guard(file);
  LIBC_NAMESPACE::libc_errno = EDOM;
  LIBC_NAMESPACE::psiginfo(&info, nullptr);
  EXPECT_STREQ(file.bytes, "Segmentation fault (code=1234)\n");
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_FALSE(file.error());
}

TEST(LlvmLibcPsiginfoTest, PayloadNumericLimits) {
  LocaleGuard locale(false);
  siginfo_t info{};
  info.si_signo = SIGRTMAX;
  info.si_code = SI_QUEUE;
  info.si_pid = INT_MAX;
  info.si_uid = UINT_MAX;
  info.si_value.sival_int = INT_MIN;
  expect_message(info, " (sent by sigqueue(), pid=2147483647, uid=4294967295, "
                       "value(int)=-2147483648)\n");
  info.si_code = SI_TIMER;
  info.si_timerid = INT_MAX;
  info.si_overrun = INT_MAX;
  expect_message(info, " (timer expired, timerid=2147483647, overrun=2147483647, "
                       "value(int)=-2147483648)\n");
  info.si_signo = SIGPOLL;
  info.si_code = POLL_IN;
  info.si_band = LONG_MIN;
  char expected[128];
  StringStream out(expected);
  out << " (input available, band=" << LONG_MIN << ")\n" << '\0';
  ASSERT_FALSE(out.overflow());
  expect_message(info, expected);
}

TEST(LlvmLibcPsiginfoTest, PreservesReturnedSignalMessage) {
  LocaleGuard locale(false);
  siginfo_t info{};
  info.si_signo = SIGTERM;
  info.si_code = SI_USER;
  for (auto orientation : ORIENTATIONS) {
    Capture file(orientation);
    StderrGuard guard(file);
    const char *prefix = LIBC_NAMESPACE::strsignal(-1234);
    ASSERT_STREQ(prefix, "Unknown signal -1234");
    LIBC_NAMESPACE::libc_errno = ERANGE;
    LIBC_NAMESPACE::psiginfo(&info, prefix);
    EXPECT_STREQ(prefix, "Unknown signal -1234");
    EXPECT_STREQ(file.bytes, "Unknown signal -1234: Terminated "
                             "(sent by kill(), pid=0, uid=0)\n");
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
    EXPECT_EQ(file.get_orientation(), orientation);
    EXPECT_FALSE(file.error());
  }
}

TEST(LlvmLibcPsiginfoTest, OutputFailureSetsErrnoAndStreamError) {
  LocaleGuard locale(false);
  siginfo_t info{};
  info.si_signo = SIGTERM;
  info.si_code = SI_QUEUE;
  info.si_pid = 123;
  info.si_uid = 456;
  info.si_value.sival_int = -789;
  unsigned char original[sizeof(info)];
  const auto *bytes = reinterpret_cast<const unsigned char *>(&info);
  for (size_t i = 0; i < sizeof(info); ++i)
    original[i] = bytes[i];

  const char expected[] = "prefix: Terminated (sent by sigqueue(), pid=123, "
                          "uid=456, value(int)=-789)\n";
  // Exercise failures in every fragment, including partial cause/payload writes
  // and the final newline, independently of byte/wide backend boundaries.
  const size_t offsets[] = {0, 3, 6, 7, 8, 12, 18, 20, 25, 38, 45,
                            sizeof(expected) - 3, sizeof(expected) - 2};
  const int errors[] = {EIO, ENOSPC, EBADF};
  for (auto orientation : ORIENTATIONS) {
    for (size_t offset : offsets) {
      for (int error : errors) {
        Capture file(orientation);
        StderrGuard guard(file);
        file.fail_after = offset;
        file.failure = error;
        LIBC_NAMESPACE::libc_errno = EDOM;
        LIBC_NAMESPACE::psiginfo(&info, "prefix");
        EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
        EXPECT_TRUE(file.error());
        EXPECT_EQ(file.get_orientation(), orientation);
        ASSERT_EQ(file.used, offset);
        for (size_t i = 0; i < offset; ++i)
          EXPECT_EQ(file.bytes[i], expected[i]);
        for (size_t i = 0; i < sizeof(info); ++i)
          EXPECT_EQ(bytes[i], original[i]);
      }
    }
  }
}

TEST(LlvmLibcPsiginfoTest, LongPrefixIsNotTruncated) {
  LocaleGuard locale(false);
  siginfo_t info{};
  info.si_signo = SIGTERM;
  info.si_code = SI_USER;
  char prefix[601];
  for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
    prefix[i] = 'x';
  prefix[sizeof(prefix) - 1] = '\0';
  for (auto orientation : ORIENTATIONS) {
    Capture file(orientation);
    StderrGuard guard(file);
    LIBC_NAMESPACE::libc_errno = ERANGE;
    LIBC_NAMESPACE::psiginfo(&info, prefix);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
    EXPECT_FALSE(file.error());
    EXPECT_EQ(file.get_orientation(), orientation);
    ASSERT_GT(file.used, sizeof(prefix) - 1);
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
      EXPECT_EQ(file.bytes[i], 'x');
    EXPECT_STREQ(file.bytes + sizeof(prefix) - 1,
                 ": Terminated (sent by kill(), pid=0, uid=0)\n");
  }
}

TEST(LlvmLibcPsiginfoTest, WidePrefixEncodingError) {
  LocaleGuard locale;
  siginfo_t info{};
  info.si_signo = SIGTERM;
  info.si_code = SI_USER;
  Capture file(File::Orientation::WIDE);
  StderrGuard guard(file);
  LIBC_NAMESPACE::libc_errno = EDOM;
  LIBC_NAMESPACE::psiginfo(&info, "\xff");
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  EXPECT_TRUE(file.error());
  EXPECT_EQ(file.used, size_t(0));
  EXPECT_EQ(file.get_orientation(), File::Orientation::WIDE);
}
