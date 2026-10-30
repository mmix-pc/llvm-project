//===-- Tests for the MMIX AIO state gate ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/threads/raw_mutex.h"
#include "test/UnitTest/Test.h"
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
static bool fail_mask = false;
static unsigned mask_calls = 0;
template <typename T, typename... Args>
T aio_gate_test_syscall(long number, Args... args) {
  ++mask_calls;
  if (fail_mask)
    return -EIO;
  return syscall_impl<T>(number, args...);
}
} // namespace LIBC_NAMESPACE_DECL

#define syscall_impl aio_gate_test_syscall
#include "src/__support/threads/linux/mmix/aio_gate.h"
#undef syscall_impl

using LIBC_NAMESPACE::mmix::AioGate;
namespace {
AioGate gate;
volatile sig_atomic_t entered = 0, active = 0, bad = 0;
void handler(int) {
  AioGate::Guard guard(gate);
  if (!guard || active)
    bad = 1;
  entered = entered + 1;
}

uint64_t raw_mask() {
  uint64_t mask = 0;
  if (LIBC_NAMESPACE::syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK,
                                         nullptr, &mask, sizeof(mask)))
    __builtin_trap();
  return mask;
}
} // namespace

TEST(LlvmLibcAioGateTest, RestoresExactMaskAndErrno) {
  uint64_t original = raw_mask();
  errno = EDOM;
  {
    AioGate::Guard guard(gate);
    ASSERT_TRUE(bool(guard));
    EXPECT_EQ(guard.error(), 0);
    auto mask = raw_mask();
    EXPECT_NE(mask & (uint64_t(1) << (SIGUSR1 - 1)), uint64_t(0));
    EXPECT_NE(mask & (uint64_t(1) << 31), uint64_t(0));
    EXPECT_EQ(mask & LIBC_NAMESPACE::mmix::CREDENTIAL_MASK,
              original & LIBC_NAMESPACE::mmix::CREDENTIAL_MASK);
  }
  EXPECT_EQ(raw_mask(), original);
  EXPECT_EQ(errno, EDOM);
}

TEST(LlvmLibcAioGateTest, PendingHandlerReentersOnlyAfterUnlock) {
  struct sigaction action{}, previous{};
  action.sa_handler = handler;
  ASSERT_EQ(::sigemptyset(&action.sa_mask), 0);
  ASSERT_EQ(::sigaction(SIGUSR1, &action, &previous), 0);
  sigset_t one, saved;
  ASSERT_EQ(::sigemptyset(&one), 0);
  ASSERT_EQ(::sigaddset(&one, SIGUSR1), 0);
  ASSERT_EQ(::sigprocmask(SIG_UNBLOCK, &one, &saved), 0);
  entered = active = bad = 0;
  {
    AioGate::Guard guard(gate);
    ASSERT_TRUE(bool(guard));
    active = 1;
    ASSERT_EQ(::raise(SIGUSR1), 0);
    EXPECT_EQ(int(entered), 0);
    active = 0;
  }
  EXPECT_EQ(int(entered), 1);
  EXPECT_EQ(int(bad), 0);
  ASSERT_EQ(::sigprocmask(SIG_SETMASK, &saved, nullptr), 0);
  ASSERT_EQ(::sigaction(SIGUSR1, &previous, nullptr), 0);
}

TEST(LlvmLibcAioGateTest, NestedIndependentGatesPreserveOuterMask) {
  AioGate inner;
  auto original = raw_mask();
  {
    AioGate::Guard outer_guard(gate);
    ASSERT_TRUE(bool(outer_guard));
    auto outer_mask = raw_mask();
    {
      AioGate::Guard inner_guard(inner);
      ASSERT_TRUE(bool(inner_guard));
      EXPECT_EQ(raw_mask(), outer_mask);
    }
    EXPECT_EQ(raw_mask(), outer_mask);
  }
  EXPECT_EQ(raw_mask(), original);
}

TEST(LlvmLibcAioGateTest, FailedMaskDoesNotLockOrRestore) {
  auto original = raw_mask();
  LIBC_NAMESPACE::mask_calls = 0;
  LIBC_NAMESPACE::fail_mask = true;
  {
    AioGate::Guard guard(gate);
    EXPECT_FALSE(bool(guard));
    EXPECT_EQ(guard.error(), EIO);
  }
  LIBC_NAMESPACE::fail_mask = false;
  EXPECT_EQ(LIBC_NAMESPACE::mask_calls, 1U);
  EXPECT_EQ(raw_mask(), original);
  AioGate::Guard retry(gate);
  EXPECT_TRUE(bool(retry));
}

TEST(LlvmLibcAioGateTest, FailedRestoreCannotReturnWithChangedMask) {
  EXPECT_DEATH(
      [] {
        AioGate local;
        AioGate::Guard guard(local);
        if (!guard)
          __builtin_trap();
        LIBC_NAMESPACE::fail_mask = true;
      },
      WITH_SIGNAL(-1));
}
