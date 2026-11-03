//===-- Signal wait wrapper tests -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/signal/sigtimedwait.h"
#include "src/signal/sigwait.h"
#include "src/signal/sigwaitinfo.h"
#include "test/UnitTest/Test.h"

static const sigset_t *expected_set;
static siginfo_t *expected_info;
static int result, interrupts, calls;

namespace LIBC_NAMESPACE_DECL {
int sigtimedwait(const sigset_t *set, siginfo_t *info,
                const struct timespec *timeout) {
  EXPECT_EQ(set, expected_set);
  EXPECT_EQ(info, expected_info);
  EXPECT_EQ(timeout, nullptr);
  ++calls;
  if (interrupts) {
    --interrupts;
    libc_errno = EINTR;
    return -1;
  }
  if (result < 0) {
    libc_errno = -result;
    return -1;
  }
  if (info)
    info->si_signo = result;
  return result;
}
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcSignalWaitDispatchTest, SigwaitRetriesOnlyInterruptions) {
  sigset_t set{};
  expected_set = &set;
  expected_info = nullptr;
  result = 35;
  interrupts = 3;
  calls = 0;
  int signo = 999;
  EXPECT_EQ(LIBC_NAMESPACE::sigwait(&set, &signo), 0);
  EXPECT_EQ(signo, 35);
  EXPECT_EQ(calls, 4);
}

TEST(LlvmLibcSignalWaitDispatchTest, SigwaitReturnsErrorWithoutChangingOutput) {
  sigset_t set{};
  expected_set = &set;
  expected_info = nullptr;
  const int errors[] = {EINVAL, EAGAIN, EFAULT};
  for (int error : errors) {
    result = -error;
    interrupts = calls = 0;
    int signo = 999;
    EXPECT_EQ(LIBC_NAMESPACE::sigwait(&set, &signo), error);
    EXPECT_EQ(signo, 999);
    EXPECT_EQ(calls, 1);
  }
}

TEST(LlvmLibcSignalWaitDispatchTest, SigwaitinfoDoesNotRetry) {
  sigset_t set{};
  siginfo_t info{};
  expected_set = &set;
  expected_info = &info;
  interrupts = 0;
  const int results[] = {35, 64, -EINTR, -EAGAIN, -EINVAL, -EFAULT};
  for (int value : results) {
    result = value;
    calls = 0;
    info.si_signo = 999;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::sigwaitinfo(&set, &info), value < 0 ? -1 : value);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(info.si_signo, value < 0 ? 999 : value);
    EXPECT_EQ(int(libc_errno), value < 0 ? -value : EDOM);
  }
  expected_info = nullptr;
  result = 35;
  EXPECT_EQ(LIBC_NAMESPACE::sigwaitinfo(&set, nullptr), 35);
}
