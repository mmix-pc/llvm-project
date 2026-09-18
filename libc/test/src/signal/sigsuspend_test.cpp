//===-- Tests for Linux sigsuspend ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/signal_macros.h"
#include "src/__support/libc_errno.h"
#include "src/signal/raise.h"
#include "src/signal/sigaction.h"
#include "src/signal/sigaddset.h"
#include "src/signal/sigemptyset.h"
#include "src/signal/sigismember.h"
#include "src/signal/sigprocmask.h"
#include "src/signal/sigsuspend.h"
#include "test/UnitTest/Test.h"
#include <signal.h>

static volatile sig_atomic_t delivered;
static void handler(int sig) { delivered = sig; }

TEST(LlvmLibcSigsuspendTest, PendingSignalAndMaskRestoration) {
  sigset_t blocked{}, old_mask{}, wait_mask{}, after{};
  struct sigaction action{}, old_action{};
  ASSERT_EQ(LIBC_NAMESPACE::sigemptyset(&blocked), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaddset(&blocked, SIGUSR1), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaddset(&blocked, SIGUSR2), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigemptyset(&wait_mask), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaddset(&wait_mask, SIGUSR2), 0);
  action.sa_handler = handler;
  action.sa_flags = SA_RESTART;
  ASSERT_EQ(LIBC_NAMESPACE::sigemptyset(&action.sa_mask), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR1, &action, &old_action), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, &blocked, &old_mask), 0);

  // Queue while blocked: no sleep, sender thread or scheduling deadline is
  // needed.
  delivered = 0;
  ASSERT_EQ(LIBC_NAMESPACE::raise(SIGUSR1), 0);
  EXPECT_EQ(int(delivered), 0);
  EXPECT_EQ(LIBC_NAMESPACE::sigsuspend(&wait_mask), -1);
  EXPECT_EQ(int(libc_errno), EINTR);
  EXPECT_EQ(int(delivered), SIGUSR1);
  EXPECT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, nullptr, &after), 0);
  EXPECT_EQ(after.__signals[0], blocked.__signals[0]);
  EXPECT_EQ(wait_mask.__signals[0], 1UL << (SIGUSR2 - 1));
  EXPECT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, &old_mask, nullptr), 0);
  EXPECT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR1, &old_action, nullptr), 0);
}

TEST(LlvmLibcSigsuspendTest, InvalidPointer) {
  EXPECT_EQ(LIBC_NAMESPACE::sigsuspend(nullptr), -1);
  EXPECT_EQ(int(libc_errno), EFAULT);
}

TEST(LlvmLibcSigsuspendTest, Membership) {
  sigset_t set{};
  for (int sig = 1; sig < NSIG; ++sig) {
    ASSERT_EQ(LIBC_NAMESPACE::sigemptyset(&set), 0);
    ASSERT_EQ(LIBC_NAMESPACE::sigaddset(&set, sig), 0);
    libc_errno = 77;
    for (int other = 1; other < NSIG; ++other)
      EXPECT_EQ(LIBC_NAMESPACE::sigismember(&set, other), int(other == sig));
    EXPECT_EQ(int(libc_errno), 77);
  }
  const int invalid[] = {-1, 0, NSIG};
  for (int sig : invalid) {
    EXPECT_EQ(LIBC_NAMESPACE::sigismember(&set, sig), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
  }
  EXPECT_EQ(LIBC_NAMESPACE::sigismember(nullptr, SIGUSR1), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
}
