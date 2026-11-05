//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/threads/thrd_sleep.h"
#include "src/threads/thrd_yield.h"
#include "src/time/clock_gettime.h"
#include "test/IntegrationTest/test.h"

#include <errno.h>
#include <time.h>

TEST_MAIN() {
  timespec begin, end, duration{0, 1000000};
  ASSERT_EQ(LIBC_NAMESPACE::clock_gettime(CLOCK_MONOTONIC, &begin), 0);
  ASSERT_EQ(LIBC_NAMESPACE::thrd_sleep(&duration, nullptr), 0);
  ASSERT_EQ(LIBC_NAMESPACE::clock_gettime(CLOCK_MONOTONIC, &end), 0);
  auto seconds = end.tv_sec - begin.tv_sec;
  auto nanoseconds = end.tv_nsec - begin.tv_nsec;
  if (nanoseconds < 0) {
    --seconds;
    nanoseconds += 1000000000;
  }
  ASSERT_TRUE(seconds > 0 || (seconds == 0 && nanoseconds >= duration.tv_nsec));
  duration = {0, 0};
  ASSERT_EQ(LIBC_NAMESPACE::thrd_sleep(&duration, nullptr), 0);
  duration = {-1, 0};
  libc_errno = 0;
  ASSERT_TRUE(LIBC_NAMESPACE::thrd_sleep(&duration, nullptr) < 0);
  ASSERT_EQ(libc_errno, EINVAL);
  duration = {0, 1000000000};
  libc_errno = 0;
  ASSERT_TRUE(LIBC_NAMESPACE::thrd_sleep(&duration, nullptr) < 0);
  ASSERT_EQ(libc_errno, EINVAL);
  LIBC_NAMESPACE::thrd_yield();
  return 0;
}
