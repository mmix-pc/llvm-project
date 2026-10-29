//===-- execlp argument forwarding tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
struct {
  unsigned calls;
  const char *file;
  const char *argv[16];
  unsigned argc;
  bool terminated;
  int incoming_errno;
  int error;
  int result;
} state;

void reset(int error = ENOENT, int result = -1) {
  state = {};
  state.error = error;
  state.result = result;
  LIBC_NAMESPACE::libc_errno = EDOM;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
int execlp_test_execvp(const char *file, char *const argv[]) {
  ++state.calls;
  state.file = file;
  state.incoming_errno = libc_errno;
  // Copy the entries while the production wrapper's stack vector is alive.
  state.argc = 0;
  state.terminated = false;
  if (argv) {
    for (unsigned i = 0; i < 16; ++i) {
      state.argv[i] = argv[i];
      if (!argv[i]) {
        state.terminated = true;
        break;
      }
      ++state.argc;
    }
  }
  libc_errno = state.error;
  return state.result;
}
} // namespace LIBC_NAMESPACE_DECL

// Exercise the public production wrapper without selecting or executing execvp.
#define execvp execlp_test_execvp
#define execlp injected_execlp
#include "src/unistd/execlp.cpp"
#undef execlp
#undef execvp

TEST(LlvmLibcExeclpTest, ForwardsFileAndIndependentArgvZero) {
  const char *files[] = {"tool", "relative/tool", "/absolute/tool", ""};
  const char arg0[] = "custom-argv-zero";
  for (const char *file : files) {
    reset();
    EXPECT_EQ(LIBC_NAMESPACE::injected_execlp(
                  file, arg0, static_cast<const char *>(nullptr)),
              -1);
    EXPECT_EQ(state.calls, 1U);
    EXPECT_EQ(state.file, file);
    EXPECT_EQ(state.argc, 1U);
    EXPECT_TRUE(state.terminated);
    EXPECT_EQ(state.argv[0], arg0);
    EXPECT_EQ(state.argv[1], nullptr);
    EXPECT_EQ(state.incoming_errno, EDOM);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  }
  EXPECT_STREQ(arg0, "custom-argv-zero");
}

TEST(LlvmLibcExeclpTest, PreservesArgumentOrderAndStopsAtSentinel) {
  const char file[] = "script";
  const char arg0[] = "custom-name";
  const char arg1[] = "";
  const char arg2[] = "two words";
  const char arg3[] = "a;b";
  const char arg4[] = "four";
  const char arg5[] = "five";
  const char arg6[] = "six";
  const char arg7[] = "seven";
  const char arg8[] = "eight";
  const char *expected[] = {arg0, arg1, arg2, arg3, arg4,
                            arg5, arg6, arg7, arg8};
  reset(EACCES);
  EXPECT_EQ(LIBC_NAMESPACE::injected_execlp(
                file, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8,
                static_cast<const char *>(nullptr), "ignored"),
            -1);
  EXPECT_EQ(state.calls, 1U);
  EXPECT_EQ(state.file, file);
  EXPECT_EQ(state.argc, 9U);
  EXPECT_TRUE(state.terminated);
  for (unsigned i = 0; i < 9; ++i)
    EXPECT_EQ(state.argv[i], expected[i]);
  EXPECT_EQ(state.argv[9], nullptr);
  EXPECT_EQ(state.incoming_errno, EDOM);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EACCES);
  EXPECT_STREQ(arg1, "");
  EXPECT_STREQ(arg2, "two words");
  EXPECT_STREQ(arg3, "a;b");
}

TEST(LlvmLibcExeclpTest, NullArgvZeroUsesExistingEmptyVectorChoice) {
  const char file[] = "tool";
  reset();
  // Like execl, null arg0 is itself the sentinel; no vararg is required.
  EXPECT_EQ(LIBC_NAMESPACE::injected_execlp(file, nullptr), -1);
  EXPECT_EQ(state.calls, 1U);
  EXPECT_EQ(state.file, file);
  EXPECT_EQ(state.argc, 0U);
  EXPECT_TRUE(state.terminated);
  EXPECT_EQ(state.argv[0], nullptr);
  EXPECT_EQ(state.incoming_errno, EDOM);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
}

TEST(LlvmLibcExeclpTest, PropagatesExecvpErrorsWithoutRetryOrFallback) {
  const char file[] = "tool";
  const char arg0[] = "name";
  const int errors[] = {ENOENT, EACCES,  ENOEXEC, EINTR, E2BIG,
                        ENOMEM, ENOTDIR, ETXTBSY, ELOOP, ENAMETOOLONG};
  for (int error : errors) {
    reset(error);
    EXPECT_EQ(LIBC_NAMESPACE::injected_execlp(
                  file, arg0, static_cast<const char *>(nullptr)),
              -1);
    EXPECT_EQ(state.calls, 1U);
    EXPECT_EQ(state.file, file);
    EXPECT_EQ(state.argc, 1U);
    EXPECT_TRUE(state.terminated);
    EXPECT_EQ(state.argv[0], arg0);
    EXPECT_EQ(state.incoming_errno, EDOM);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
  }
}

TEST(LlvmLibcExeclpTest, ForwardsInjectedReturnValue) {
  reset(EDOM, 7);
  // A real successful exec never returns; this checks only wrapper forwarding.
  EXPECT_EQ(LIBC_NAMESPACE::injected_execlp("tool", "name",
                                            static_cast<const char *>(nullptr)),
            7);
  EXPECT_EQ(state.calls, 1U);
  EXPECT_TRUE(state.terminated);
  EXPECT_EQ(state.incoming_errno, EDOM);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}
