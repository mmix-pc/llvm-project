//===-- Tests for raise error propagation ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/rt_sigprocmask.h"
#include "test/UnitTest/Test.h"

static int step;
static long results[5];
namespace LIBC_NAMESPACE_DECL {
template <typename R, typename... Args>
R test_raise_syscall(long number, Args... args) {
  const long values[4] = {0, static_cast<long>(args)...};
  if (step == 1) {
    EXPECT_EQ(number, static_cast<long>(SYS_getpid));
  } else if (step == 2) {
    EXPECT_EQ(number, static_cast<long>(SYS_gettid));
  } else {
    EXPECT_EQ(step, 3);
    EXPECT_EQ(number, static_cast<long>(SYS_tgkill));
    EXPECT_EQ(values[1], 101L);
    EXPECT_EQ(values[2], 202L);
    EXPECT_EQ(values[3], static_cast<long>(SIGCONT));
  }
  return static_cast<R>(results[step++]);
}
namespace linux_syscalls {
ErrorOr<int> test_raise_sigprocmask(int how, const sigset_t *set, sigset_t *old) {
  EXPECT_TRUE(set != nullptr);
  if (step == 0) {
    EXPECT_EQ(how, SIG_BLOCK);
    EXPECT_TRUE(old != nullptr);
    if (old)
      *old = {};
    ++step;
    return results[0] < 0 ? ErrorOr<int>(Error(-static_cast<int>(results[0])))
                          : ErrorOr<int>(0);
  }
  EXPECT_EQ(how, SIG_SETMASK);
  EXPECT_TRUE(old == nullptr);
  step = 5;
  return results[4] < 0 ? ErrorOr<int>(Error(-static_cast<int>(results[4])))
                        : ErrorOr<int>(0);
}
} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

// Include dependencies first, then redirect only the wrapper's syscall calls.
// No real signal mask or process state is changed by failure injection.
#define syscall_impl test_raise_syscall
#define rt_sigprocmask test_raise_sigprocmask
#include "src/__support/OSUtil/linux/syscall_wrappers/raise.h"
#undef rt_sigprocmask
#undef syscall_impl

TEST(LlvmLibcRaiseErrorTest, PropagatesFailuresAndRestoresMask) {
  for (int failure = -1; failure != 5; ++failure) {
    step = 0;
    results[0] = 0;
    results[1] = 101;
    results[2] = 202;
    results[3] = results[4] = 0;
    if (failure >= 0)
      results[failure] = -22;
    auto result = LIBC_NAMESPACE::linux_syscalls::raise(SIGCONT);
    EXPECT_EQ(result.has_value(), failure == -1);
    if (!result.has_value())
      EXPECT_EQ(result.error(), 22);
    else
      EXPECT_EQ(result.value(), 0);
    EXPECT_EQ(step, failure == 0 ? 1 : 5);
  }
}
