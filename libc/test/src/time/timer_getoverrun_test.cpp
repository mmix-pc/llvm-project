//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/time_macros.h"
#include "hdr/types/struct_itimerspec.h"
#include "hdr/types/struct_sigevent.h"
#include "hdr/types/timer_t.h"
#include "src/__support/libc_errno.h"
#include "src/time/clock_gettime.h"
#include "src/time/timer_create.h"
#include "src/time/timer_delete.h"
#include "src/time/timer_getoverrun.h"
#include "src/time/timer_gettime.h"
#include "src/time/timer_settime.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Fails;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::GE;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::returns;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Succeeds;

class LlvmLibcTimerGetoverrunTest
    : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
protected:
  timer_t timerid{};
  // A null pointer can represent the valid kernel timer ID zero.
  bool timer_created = false;

  int createTimer() {
    sigevent event{};
    event.sigev_notify = SIGEV_NONE;
    int result =
        LIBC_NAMESPACE::timer_create(CLOCK_MONOTONIC, &event, &timerid);
    timer_created = result == 0;
    return result;
  }

public:
  void TearDown() override {
    if (timer_created)
      EXPECT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::TearDown();
  }
};

TEST_F(LlvmLibcTimerGetoverrunTest, Disarmed) {
  ASSERT_THAT(createTimer(), Succeeds(0));
  EXPECT_THAT(LIBC_NAMESPACE::timer_getoverrun(timerid), Succeeds(0));

  itimerspec disarm{};
  ASSERT_THAT(LIBC_NAMESPACE::timer_settime(timerid, 0, &disarm, nullptr),
              Succeeds(0));
  libc_errno = EDOM;
  EXPECT_THAT(LIBC_NAMESPACE::timer_getoverrun(timerid), Succeeds(0, EDOM));
}

TEST_F(LlvmLibcTimerGetoverrunTest, ExpiredOneShot) {
  ASSERT_THAT(createTimer(), Succeeds(0));

  itimerspec value{};
  ASSERT_THAT(LIBC_NAMESPACE::clock_gettime(CLOCK_MONOTONIC, &value.it_value),
              Succeeds(0));
  ASSERT_TRUE(value.it_value.tv_sec != 0 || value.it_value.tv_nsec != 0);
  // This nonzero absolute deadline has already been reached. Linux computes
  // SIGEV_NONE one-shot expiry in timer_gettime without sleeps or polling.
  ASSERT_THAT(
      LIBC_NAMESPACE::timer_settime(timerid, TIMER_ABSTIME, &value, nullptr),
      Succeeds(0));

  itimerspec remaining{};
  ASSERT_THAT(LIBC_NAMESPACE::timer_gettime(timerid, &remaining), Succeeds(0));
  ASSERT_EQ(remaining.it_value.tv_sec, static_cast<time_t>(0));
  ASSERT_EQ(remaining.it_value.tv_nsec,
            decltype(remaining.it_value.tv_nsec)(0));
  // No notification is delivered with SIGEV_NONE. Expiry alone does not imply
  // a positive overrun count, which describes notification delivery instead.
  EXPECT_THAT(LIBC_NAMESPACE::timer_getoverrun(timerid),
              returns(GE(0)).with_errno(0));
}

TEST_F(LlvmLibcTimerGetoverrunTest, InvalidTimerId) {
  EXPECT_THAT(LIBC_NAMESPACE::timer_getoverrun(reinterpret_cast<timer_t>(-1L)),
              Fails(EINVAL));
}

TEST_F(LlvmLibcTimerGetoverrunTest, DeletedTimerId) {
  ASSERT_THAT(createTimer(), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
  timer_created = false;

  EXPECT_THAT(LIBC_NAMESPACE::timer_getoverrun(timerid), Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Fails(EINVAL));
}
