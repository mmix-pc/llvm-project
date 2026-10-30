//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unittests for posix timers (timer_create, timer_delete).
///
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/time_macros.h"
#include "hdr/types/clockid_t.h"
#include "hdr/types/struct_itimerspec.h"
#include "hdr/types/struct_sigevent.h"
#include "hdr/types/timer_t.h"
#include "src/time/clock_gettime.h"
#include "src/time/timer_create.h"
#include "src/time/timer_delete.h"
#include "src/time/timer_gettime.h"
#include "src/time/timer_settime.h"
#include "src/unistd/gettid.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::any_of;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Fails;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Succeeds;
using LlvmLibcTimerTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcTimerTest, NullSigevent) {
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, &timerid),
              Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

TEST_F(LlvmLibcTimerTest, ValidSigeventSigevNone) {
  sigevent se;
  se.sigev_notify = SIGEV_NONE;
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_MONOTONIC, &se, &timerid),
              Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

TEST_F(LlvmLibcTimerTest, CreateInitializesEntireHandle) {
  sigevent se{};
  se.sigev_notify = SIGEV_NONE;
  sigevent *events[] = {nullptr, &se};
  for (sigevent *event : events) {
    struct {
      unsigned long before;
      timer_t timerid;
      unsigned long after;
    } storage{0x12345678UL, reinterpret_cast<timer_t>(-1L), 0x87654321UL};
    ASSERT_THAT(
        LIBC_NAMESPACE::timer_create(CLOCK_MONOTONIC, event, &storage.timerid),
        Succeeds(0));

    // Kernel timer IDs are nonnegative ints. A partial write into the nonzero
    // pointer-sized output violates this bound on both 64-bit byte orders,
    // even when the kernel returns ID zero.
    EXPECT_LE(reinterpret_cast<unsigned long>(storage.timerid),
              static_cast<unsigned long>(~0U >> 1));
    EXPECT_EQ(storage.before, 0x12345678UL);
    EXPECT_EQ(storage.after, 0x87654321UL);

    itimerspec value{};
    EXPECT_THAT(LIBC_NAMESPACE::timer_gettime(storage.timerid, &value),
                Succeeds(0));
    EXPECT_EQ(value.it_value.tv_sec, static_cast<time_t>(0));
    EXPECT_EQ(value.it_value.tv_nsec, decltype(value.it_value.tv_nsec)(0));
    itimerspec disarm{};
    EXPECT_THAT(
        LIBC_NAMESPACE::timer_settime(storage.timerid, 0, &disarm, nullptr),
        Succeeds(0));
    EXPECT_THAT(LIBC_NAMESPACE::timer_delete(storage.timerid), Succeeds(0));
  }
}

TEST_F(LlvmLibcTimerTest, ValidSigeventSigevSignal) {
  sigevent se;
  se.sigev_notify = SIGEV_SIGNAL;
  se.sigev_signo = SIGALRM;
  se.sigev_value.sival_int = 42;
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, &se, &timerid),
              Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

#ifdef SIGEV_THREAD_ID
TEST_F(LlvmLibcTimerTest, ValidSigeventSigevThreadId) {
  sigevent se;
  se.sigev_notify = SIGEV_THREAD_ID;
  se.sigev_signo = SIGALRM;
  se.sigev_value.sival_int = 42;
#if !defined(LIBC_FULL_BUILD) && defined(__GLIBC__)
  se._sigev_un._tid = LIBC_NAMESPACE::gettid();
#else
  se.sigev_notify_thread_id = LIBC_NAMESPACE::gettid();
#endif
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, &se, &timerid),
              Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}
#endif

TEST_F(LlvmLibcTimerTest, InvalidClockId) {
  sigevent se{};
  se.sigev_notify = SIGEV_NONE;
  sigevent *events[] = {nullptr, &se};
  for (sigevent *event : events) {
    const timer_t sentinel = reinterpret_cast<timer_t>(-1L);
    timer_t timerid = sentinel;
    EXPECT_THAT(LIBC_NAMESPACE::timer_create(static_cast<clockid_t>(-1), event,
                                            &timerid),
                Fails(EINVAL));
    EXPECT_EQ(timerid, sentinel);
  }
}

TEST_F(LlvmLibcTimerTest, NullTimerId) {
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, nullptr),
              Fails(any_of(EINVAL, EFAULT)));
  sigevent se{};
  se.sigev_notify = SIGEV_NONE;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, &se, nullptr),
              Fails(any_of(EINVAL, EFAULT)));
}

TEST_F(LlvmLibcTimerTest, DeleteInvalidTimerId) {
  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(reinterpret_cast<timer_t>(-1)),
              Fails(EINVAL));
}

TEST_F(LlvmLibcTimerTest, CreateAndDelete) {
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, &timerid),
              Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Fails(EINVAL));
}

TEST_F(LlvmLibcTimerTest, SetTimeRelativeAndDisarm) {
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, &timerid),
              Succeeds(0));

  itimerspec new_val{};
  new_val.it_value.tv_sec = 10;
  new_val.it_value.tv_nsec = 0;
  new_val.it_interval.tv_sec = 1;
  new_val.it_interval.tv_nsec = 0;

  itimerspec old_val{};
  ASSERT_THAT(LIBC_NAMESPACE::timer_settime(timerid, 0, &new_val, &old_val),
              Succeeds(0));
  // Initially, old_val should be 0 because it was disarmed.
  ASSERT_EQ(old_val.it_value.tv_sec, static_cast<time_t>(0));
  ASSERT_EQ(old_val.it_value.tv_nsec,
            static_cast<decltype(old_val.it_value.tv_nsec)>(0));

  // Disarm the timer by setting it_value to 0
  itimerspec disarm_val{};
  ASSERT_THAT(LIBC_NAMESPACE::timer_settime(timerid, 0, &disarm_val, &old_val),
              Succeeds(0));
  // old_val should have been armed previously
  ASSERT_GT(old_val.it_value.tv_sec, static_cast<time_t>(0));

  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

TEST_F(LlvmLibcTimerTest, SetTimeAbsolute) {
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, &timerid),
              Succeeds(0));

  timespec now{};
  ASSERT_THAT(LIBC_NAMESPACE::clock_gettime(CLOCK_REALTIME, &now), Succeeds(0));

  itimerspec new_val{};
  new_val.it_value.tv_sec = now.tv_sec + 100;
  new_val.it_value.tv_nsec = 0;

  ASSERT_THAT(
      LIBC_NAMESPACE::timer_settime(timerid, TIMER_ABSTIME, &new_val, nullptr),
      Succeeds(0));

  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

TEST_F(LlvmLibcTimerTest, SetTimeInvalidTimerId) {
  itimerspec new_val{};
  new_val.it_value.tv_sec = 1;
  ASSERT_THAT(LIBC_NAMESPACE::timer_settime(reinterpret_cast<timer_t>(-1), 0,
                                            &new_val, nullptr),
              Fails(EINVAL));
}

TEST_F(LlvmLibcTimerTest, SetTimeNullNewValue) {
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, &timerid),
              Succeeds(0));

  ASSERT_THAT(LIBC_NAMESPACE::timer_settime(timerid, 0, nullptr, nullptr),
              Fails(any_of(EINVAL, EFAULT)));

  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

TEST_F(LlvmLibcTimerTest, SetTimeInvalidNanoseconds) {
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, &timerid),
              Succeeds(0));

  itimerspec new_val{};
  new_val.it_value.tv_sec = 1;
  new_val.it_value.tv_nsec = 1000000000; // invalid >= 10^9

  ASSERT_THAT(LIBC_NAMESPACE::timer_settime(timerid, 0, &new_val, nullptr),
              Fails(EINVAL));

  new_val.it_value.tv_nsec = -1; // invalid < 0
  ASSERT_THAT(LIBC_NAMESPACE::timer_settime(timerid, 0, &new_val, nullptr),
              Fails(EINVAL));

  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

TEST_F(LlvmLibcTimerTest, GetTimeDisarmedAndArmed) {
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, &timerid),
              Succeeds(0));

  itimerspec curr_val{};
  ASSERT_THAT(LIBC_NAMESPACE::timer_gettime(timerid, &curr_val), Succeeds(0));
  ASSERT_EQ(curr_val.it_value.tv_sec, static_cast<time_t>(0));
  ASSERT_EQ(curr_val.it_value.tv_nsec,
            static_cast<decltype(curr_val.it_value.tv_nsec)>(0));

  itimerspec new_val{};
  new_val.it_value.tv_sec = 20;
  new_val.it_value.tv_nsec = 0;
  new_val.it_interval.tv_sec = 2;
  new_val.it_interval.tv_nsec = 0;

  ASSERT_THAT(LIBC_NAMESPACE::timer_settime(timerid, 0, &new_val, nullptr),
              Succeeds(0));

  ASSERT_THAT(LIBC_NAMESPACE::timer_gettime(timerid, &curr_val), Succeeds(0));
  ASSERT_GT(curr_val.it_value.tv_sec, static_cast<time_t>(0));
  ASSERT_EQ(curr_val.it_interval.tv_sec, static_cast<time_t>(2));

  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

TEST_F(LlvmLibcTimerTest, GetTimeInvalidTimerId) {
  itimerspec curr_val{};
  ASSERT_THAT(
      LIBC_NAMESPACE::timer_gettime(reinterpret_cast<timer_t>(-1), &curr_val),
      Fails(EINVAL));
}

TEST_F(LlvmLibcTimerTest, GetTimeNullPointer) {
  timer_t timerid;
  ASSERT_THAT(LIBC_NAMESPACE::timer_create(CLOCK_REALTIME, nullptr, &timerid),
              Succeeds(0));

  ASSERT_THAT(LIBC_NAMESPACE::timer_gettime(timerid, nullptr),
              Fails(any_of(EINVAL, EFAULT)));

  ASSERT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
}

#ifndef LIBC_FULL_BUILD
TEST_F(LlvmLibcTimerTest, HostTimersCoexist) {
  sigevent se{};
  se.sigev_notify = SIGEV_NONE;
  timer_t host_timer;
  ASSERT_EQ(::timer_create(CLOCK_MONOTONIC, &se, &host_timer), 0);

  timer_t timerid = reinterpret_cast<timer_t>(-1L);
  int result = LIBC_NAMESPACE::timer_create(CLOCK_MONOTONIC, &se, &timerid);
  EXPECT_THAT(result, Succeeds(0));
  if (result != 0) {
    EXPECT_EQ(::timer_delete(host_timer), 0);
    return;
  }

  itimerspec host_value{};
  host_value.it_value.tv_sec = 3600;
  host_value.it_interval.tv_sec = 3;
  EXPECT_EQ(::timer_settime(host_timer, 0, &host_value, nullptr), 0);

  itimerspec value{};
  value.it_value.tv_sec = 3600;
  value.it_interval.tv_sec = 7;
  EXPECT_THAT(LIBC_NAMESPACE::timer_settime(timerid, 0, &value, nullptr),
              Succeeds(0));

  // Each implementation owns its handles; do not exchange them with the host.
  itimerspec current{};
  EXPECT_EQ(::timer_gettime(host_timer, &current), 0);
  EXPECT_EQ(current.it_interval.tv_sec, static_cast<time_t>(3));
  EXPECT_THAT(LIBC_NAMESPACE::timer_gettime(timerid, &current), Succeeds(0));
  EXPECT_EQ(current.it_interval.tv_sec, static_cast<time_t>(7));

  EXPECT_THAT(LIBC_NAMESPACE::timer_delete(timerid), Succeeds(0));
  EXPECT_EQ(::timer_gettime(host_timer, &current), 0);
  EXPECT_EQ(current.it_interval.tv_sec, static_cast<time_t>(3));
  EXPECT_EQ(::timer_delete(host_timer), 0);
}
#endif
