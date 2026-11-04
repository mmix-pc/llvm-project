//===-- MMIX sleep cancellation transport tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/types/struct_timespec.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
long reply;
unsigned calls, seconds;
bool write_remaining;
timespec remaining_value;
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
long cancellation_syscall(unsigned long number, long request, long remaining,
                          long a3, long a4, long a5, long a6) {
  EXPECT_EQ(number, static_cast<unsigned long>(SYS_nanosleep));
  EXPECT_EQ(a3 | a4 | a5 | a6, 0L);
  EXPECT_EQ(reinterpret_cast<timespec *>(request)->tv_sec, time_t(seconds));
  EXPECT_EQ(reinterpret_cast<timespec *>(request)->tv_nsec, 0L);
  ++calls;
  if (write_remaining)
    *reinterpret_cast<timespec *>(remaining) = remaining_value;
  return reply;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#include "src/unistd/linux/mmix/cancellation_sleep.cpp"

TEST(LlvmLibcMmixSleep, InterruptedBeforeKernelEntry) {
  const unsigned inputs[] = {0U, 1U, 17U, UINT_MAX};
  for (unsigned value : inputs) {
    seconds = value;
    calls = 0;
    reply = -EINTR;
    write_remaining = false;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::sleep(seconds), seconds);
    EXPECT_EQ(calls, 1U);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST(LlvmLibcMmixSleep, KernelRemainderReplacesInitialValue) {
  seconds = 17;
  const timespec remainders[] = {{0, 900000000}, {3, 250000000}, {17, 0}};
  for (timespec value : remainders) {
    calls = 0;
    reply = -EINTR;
    write_remaining = true;
    remaining_value = value;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::sleep(seconds),
              static_cast<unsigned>(value.tv_sec));
    EXPECT_EQ(calls, 1U);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST(LlvmLibcMmixSleep, SuccessfulSleepReturnsZero) {
  seconds = 17;
  calls = 0;
  reply = 0;
  write_remaining = false;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sleep(seconds), 0U);
  EXPECT_EQ(calls, 1U);
  EXPECT_EQ(int(libc_errno), EDOM);
}
