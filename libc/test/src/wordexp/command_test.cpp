//===-- Word command capture tests ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_wait_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/freelist_heap.h"
#include "src/wordexp/command.h"
#include "src/wordexp/shell_syntax.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

using LIBC_NAMESPACE::syscall_impl;
using LIBC_NAMESPACE::cpp::string_view;
using LIBC_NAMESPACE::internal::capture_word_command;

namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0;
bool fail() { return fail_after >= 0 && fail_after-- == 0; }

struct Output {
  char prefix[128]{};
  size_t length = 0;
  int failure = 0;
  static int append(void *p, string_view bytes) {
    auto &out = *static_cast<Output *>(p);
    if (out.failure)
      return out.failure;
    for (char c : bytes) {
      if (out.length < sizeof(out.prefix) - 1)
        out.prefix[out.length] = c;
      ++out.length;
    }
    return 0;
  }
};

char variable[] = "VALUE=from environment";
char *environment[] = {variable, nullptr};

int descriptors() {
  int count = 0;
  for (int fd = 0; fd < 256; ++fd)
    if (syscall_impl<long>(SYS_fcntl, fd, F_GETFD, 0) >= 0)
      ++count;
  return count;
}
} // namespace

extern "C" void *__wrap_malloc(size_t size) {
  if (fail())
    return nullptr;
  void *p = heap.allocate(size);
  live += p != nullptr;
  return p;
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (fail())
    return nullptr;
  void *p = heap.realloc(old, size);
  live += p && !old;
  return p;
}
extern "C" void __wrap_free(void *p) {
  live -= p != nullptr;
  heap.free(p);
}

TEST(LlvmLibcWordCommandTest, RawOutputEnvironmentAndWaitStatus) {
  Output out;
  auto status =
      capture_word_command("printf '%s\\n\\n' \"$VALUE\"; exit 7", environment,
                           false, {&out, Output::append});
  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(WIFEXITED(*status));
  EXPECT_EQ(WEXITSTATUS(*status), 7);
  EXPECT_STREQ(out.prefix, "from environment\n\n");
  EXPECT_EQ(syscall_impl<long>(SYS_wait4, -1, nullptr, WNOHANG, nullptr),
            -long(ECHILD));
}

TEST(LlvmLibcWordCommandTest, DrainBeyondPipeCapacity) {
  int before = descriptors();
  Output out;
  auto status = capture_word_command(
      "data=0123456789abcdef0123456789abcdef; i=0; "
      "while [ \"$i\" -lt 12 ]; do data=$data$data; i=$((i+1)); done; "
      "printf '%s' \"$data\"",
      environment, false, {&out, Output::append});
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(*status, 0);
  EXPECT_EQ(out.length, size_t(4096 * 32));
  EXPECT_EQ(descriptors(), before);
}

TEST(LlvmLibcWordCommandTest, SinkFailureClosesBeforeReaping) {
  int before = descriptors();
  Output out;
  out.failure = ENOMEM;
  auto status = capture_word_command(
      "data=0123456789abcdef0123456789abcdef; i=0; "
      "while [ \"$i\" -lt 12 ]; do data=$data$data; i=$((i+1)); done; "
      "printf '%s' \"$data\"",
      environment, false, {&out, Output::append});
  ASSERT_FALSE(status.has_value());
  EXPECT_EQ(status.error(), ENOMEM);
  EXPECT_EQ(descriptors(), before);
  EXPECT_EQ(syscall_impl<long>(SYS_wait4, -1, nullptr, WNOHANG, nullptr),
            -long(ECHILD));
}

TEST(LlvmLibcWordCommandTest, ClosedStandardDescriptors) {
  for (int closed = 0; closed <= 2; ++closed) {
    int before = descriptors();
    long saved = syscall_impl<long>(SYS_fcntl, closed, F_DUPFD, 3);
    ASSERT_TRUE(saved >= 0);
    ASSERT_EQ(syscall_impl<long>(SYS_close, closed), 0L);
    Output out;
    auto status = capture_word_command("printf result", environment, false,
                                       {&out, Output::append});
    // Restore the test framework's descriptors before reporting any failure.
    long restored = syscall_impl<long>(SYS_dup3, saved, closed, 0);
    (void)syscall_impl<long>(SYS_close, saved);
    ASSERT_EQ(restored, long(closed));
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(*status, 0);
    EXPECT_STREQ(out.prefix, "result");
    EXPECT_EQ(descriptors(), before);
  }
}

TEST(LlvmLibcWordCommandTest, StderrPolicy) {
  const bool modes[] = {false, true};
  for (bool show : modes) {
    int fds[2];
    ASSERT_EQ(syscall_impl<long>(SYS_pipe2, fds, O_CLOEXEC | O_NONBLOCK), 0L);
    long saved = syscall_impl<long>(SYS_fcntl, 2, F_DUPFD, 3);
    ASSERT_TRUE(saved >= 0);
    ASSERT_EQ(syscall_impl<long>(SYS_dup3, fds[1], 2, 0), 2L);
    (void)syscall_impl<long>(SYS_close, fds[1]);
    Output out;
    auto status =
        capture_word_command("printf diagnostic >&2; printf result",
                             environment, show, {&out, Output::append});
    long restored = syscall_impl<long>(SYS_dup3, saved, 2, 0);
    (void)syscall_impl<long>(SYS_close, saved);
    char bytes[32]{};
    long count = syscall_impl<long>(SYS_read, fds[0], bytes, sizeof(bytes));
    (void)syscall_impl<long>(SYS_close, fds[0]);
    ASSERT_EQ(restored, 2L);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(*status, 0);
    EXPECT_STREQ(out.prefix, "result");
    EXPECT_EQ(count, show ? 10L : 0L);
    if (show)
      EXPECT_STREQ(bytes, "diagnostic");
  }
}

TEST(LlvmLibcWordCommandTest, InvalidArgumentsHaveNoEffects) {
  int before = descriptors();
  Output out;
  auto status =
      capture_word_command(nullptr, environment, false, {&out, Output::append});
  ASSERT_FALSE(status.has_value());
  EXPECT_EQ(status.error(), EINVAL);
  status = capture_word_command("printf bad", nullptr, false,
                                {&out, Output::append});
  ASSERT_FALSE(status.has_value());
  EXPECT_EQ(status.error(), EINVAL);
  status = capture_word_command("printf bad", environment, false, {});
  ASSERT_FALSE(status.has_value());
  EXPECT_EQ(status.error(), EINVAL);
  EXPECT_EQ(out.length, size_t(0));
  EXPECT_EQ(descriptors(), before);
}

TEST(LlvmLibcWordCommandTest, AllocationFailureClosesActionsAndDescriptors) {
  int before_fds = descriptors();
  size_t before = live;
  bool complete = false;
  for (int failure = 0; failure < 64 && !complete; ++failure) {
    Output out;
    fail_after = failure;
    auto status = capture_word_command("printf result", environment, false,
                                       {&out, Output::append});
    fail_after = -1;
    complete = status.has_value();
    if (complete) {
      EXPECT_EQ(*status, 0);
      EXPECT_STREQ(out.prefix, "result");
    } else {
      EXPECT_EQ(status.error(), ENOMEM);
    }
    EXPECT_EQ(live, before);
    EXPECT_EQ(descriptors(), before_fds);
    EXPECT_EQ(syscall_impl<long>(SYS_wait4, -1, nullptr, WNOHANG, nullptr),
              -long(ECHILD));
  }
  EXPECT_TRUE(complete);
}

TEST(LlvmLibcWordCommandTest, RawCaptureDoesNotSelectNulPolicy) {
  Output out;
  auto status = capture_word_command("printf 'a\\000b\\n\\n'", environment,
                                     false, {&out, Output::append});
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(*status, 0);
  EXPECT_EQ(out.length, size_t(5));
  EXPECT_TRUE(string_view(out.prefix, 5) == string_view("a\0b\n\n", 5));
}

TEST(LlvmLibcWordCommandTest,
     SourceTokensUseTheSameLegacyEscapeRulesAsPreflight) {
  using namespace LIBC_NAMESPACE::internal;
  struct Case {
    const char *source, *expected;
  } cases[] = {
      {R"cmd("`printf '%s' \"$VALUE\"`")cmd", "from environment"},
      {R"cmd($(case x in x) printf selected;; esac))cmd", "selected"},
      {"$(read x <<'EOF'\nliteral )\nEOF\nprintf '%s' \"$x\")", "literal )"},
      {R"cmd(`printf '%s' \`printf nested\``)cmd", "nested"},
  };
  int before_fds = descriptors();
  size_t before = live;
  for (auto row : cases) {
    WordSource source;
    ASSERT_EQ(source.assign(row.source, RestartableConverter(true)), 0);
    struct Context {
      WordSource &source;
      Output output;
      unsigned commands = 0;
    } context{source, {}};
    WordSyntaxSink sink{&context, [](void *p, WordToken token) {
                          auto &c = *static_cast<Context *>(p);
                          if (token.kind != WordToken::Kind::COMMAND &&
                              token.kind != WordToken::Kind::BACKQUOTED_COMMAND)
                            return 0;
                          ++c.commands;
                          auto status = capture_word_command(
                              c.source, token, RestartableConverter(true),
                              environment, false, {&c.output, Output::append});
                          return status ? (*status == 0 ? 0 : EIO)
                                        : status.error();
                        }};
    ASSERT_TRUE(bool(parse_word_syntax(source.scalars(), false, sink,
                                       word_command_syntax())));
    EXPECT_EQ(context.commands, 1U);
    EXPECT_STREQ(context.output.prefix, row.expected);
    EXPECT_EQ(descriptors(), before_fds);
    EXPECT_EQ(syscall_impl<long>(SYS_wait4, -1, nullptr, WNOHANG, nullptr),
              -long(ECHILD));
  }
  EXPECT_EQ(live, before);
}
