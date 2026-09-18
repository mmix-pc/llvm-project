//===-- Tests for Linux sigtimedwait --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/signal_macros.h"
#include "src/__support/libc_errno.h"
#include "src/signal/raise.h"
#include "src/signal/sigaddset.h"
#include "src/signal/sigemptyset.h"
#include "src/signal/sigprocmask.h"
#include "src/signal/sigtimedwait.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcSigtimedwaitTest, PendingSignalAndPoll) {
  sigset_t set{}, old{}, after{};
  ASSERT_EQ(LIBC_NAMESPACE::sigemptyset(&set), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaddset(&set, SIGUSR1), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_BLOCK, &set, &old), 0);
  // Queue before waiting so the test does not depend on scheduling deadlines.
  ASSERT_EQ(LIBC_NAMESPACE::raise(SIGUSR1), 0);
  siginfo_t info{};
  const timespec zero{0, 0};
  libc_errno = 77;
  EXPECT_EQ(LIBC_NAMESPACE::sigtimedwait(&set, &info, &zero), SIGUSR1);
  EXPECT_EQ(int(libc_errno), 77);
  EXPECT_EQ(info.si_signo, SIGUSR1);
  EXPECT_EQ(zero.tv_sec, time_t(0));
  EXPECT_EQ(zero.tv_nsec, static_cast<__INT64_TYPE__>(0));
  EXPECT_EQ(LIBC_NAMESPACE::sigtimedwait(&set, nullptr, &zero), -1);
  EXPECT_EQ(int(libc_errno), EAGAIN);
  ASSERT_EQ(LIBC_NAMESPACE::raise(SIGUSR1), 0);
  EXPECT_EQ(LIBC_NAMESPACE::sigtimedwait(&set, nullptr, nullptr), SIGUSR1);
  EXPECT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, nullptr, &after), 0);
  EXPECT_EQ(after.__signals[0], old.__signals[0] | set.__signals[0]);
  EXPECT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, &old, nullptr), 0);
}

TEST(LlvmLibcSigtimedwaitTest, InvalidInputs) {
  sigset_t set{};
  ASSERT_EQ(LIBC_NAMESPACE::sigemptyset(&set), 0);
  const timespec invalid[] = {{-1, 0}, {0, -1}, {0, 1000000000}};
  for (const auto &timeout : invalid) {
    EXPECT_EQ(LIBC_NAMESPACE::sigtimedwait(&set, nullptr, &timeout), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
  }
  const timespec zero{0, 0};
  EXPECT_EQ(LIBC_NAMESPACE::sigtimedwait(nullptr, nullptr, &zero), -1);
  EXPECT_EQ(int(libc_errno), EFAULT);
}
