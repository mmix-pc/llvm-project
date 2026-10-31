//===-- Tests for private MMIX AIO relative waits -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/linux/mmix/aio_service.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <initializer_list>

using LIBC_NAMESPACE::AioWaitEvent;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::Futex;
using LIBC_NAMESPACE::FutexWordType;
using LIBC_NAMESPACE::cpp::numeric_limits;
using LIBC_NAMESPACE::mmix::aio_service_detail::wait_relative;

namespace {
struct Probe {
  AioWaitEvent event;
  timespec now{10, 900000000};
  timespec expected{};
  timespec *mutate = nullptr;
  unsigned clocks = 0;
  unsigned waits = 0;
  unsigned retries = 0;
  int clock_error = 0;
  int result = -ETIMEDOUT;
  bool unbounded = false;
  bool ready_at_clock = false;
  bool ready_at_wait = false;
  const void *first_timeout = nullptr;

  static ErrorOr<int> wake(void *, Futex &) { return 0; }
  void publish() { event.publish(wake); }
  static int wait(void *context, Futex *, int operation, FutexWordType value,
                  const void *timeout, unsigned bitset) {
    auto &self = *static_cast<Probe *>(context);
    EXPECT_EQ(operation, FUTEX_WAIT_BITSET_PRIVATE);
    EXPECT_EQ(value, FutexWordType(0));
    EXPECT_EQ(bitset, unsigned(FUTEX_BITSET_MATCH_ANY));
    EXPECT_EQ(timeout == nullptr, self.unbounded);
    if (!self.waits)
      self.first_timeout = timeout;
    EXPECT_EQ(timeout, self.first_timeout);
    if (timeout) {
#if defined(SYS_futex_time64)
      const auto &absolute = *static_cast<const __kernel_timespec *>(timeout);
#else
      const auto &absolute = *static_cast<const timespec *>(timeout);
#endif
      EXPECT_EQ(int64_t(absolute.tv_sec), int64_t(self.expected.tv_sec));
      EXPECT_EQ(int64_t(absolute.tv_nsec), int64_t(self.expected.tv_nsec));
    }
    if (self.mutate)
      *self.mutate = {-1, -1};
    self.now = {-1, -1};
    if (self.ready_at_wait)
      self.publish();
    ++self.waits;
    if (self.waits <= self.retries)
      return self.waits % 2 ? -EAGAIN : 0;
    return self.result;
  }
  int run(const timespec *timeout) {
    int saved_errno = libc_errno;
    libc_errno = EDOM;
    int error = wait_relative(
        event, timeout,
        [this](timespec &sample) {
          ++clocks;
          sample = now;
          if (ready_at_clock)
            publish();
          return clock_error;
        },
        wait, this);
    EXPECT_EQ(int(libc_errno), EDOM);
    libc_errno = saved_errno;
    return error;
  }
};
} // namespace

TEST(LlvmLibcAioServiceRelativeTest, ReadySkipsValidationClockAndWait) {
  Probe probe;
  probe.clock_error = EIO;
  probe.publish();
  const timespec invalid{-1, 1000000000};
  EXPECT_EQ(probe.run(&invalid), 0);
  EXPECT_EQ(probe.run(nullptr), 0);
  EXPECT_EQ(probe.clocks, 0U);
  EXPECT_EQ(probe.waits, 0U);
}

TEST(LlvmLibcAioServiceRelativeTest, InvalidIntervalsDoNotSampleOrWait) {
  const timespec invalid[] = {
      {-1, 0}, {numeric_limits<time_t>::min(), 0}, {0, -1},
      {0, 1000000000}, {0, numeric_limits<long>::max()}};
  for (const auto &interval : invalid) {
    Probe probe;
    EXPECT_EQ(probe.run(&interval), EINVAL);
    EXPECT_EQ(probe.clocks, 0U);
    EXPECT_EQ(probe.waits, 0U);
  }
}

TEST(LlvmLibcAioServiceRelativeTest, NullRemainsUnboundedWithoutClock) {
  Probe probe;
  probe.unbounded = true;
  probe.clock_error = EIO;
  probe.result = -EINTR;
  EXPECT_EQ(probe.run(nullptr), EINTR);
  EXPECT_EQ(probe.clocks, 0U);
  EXPECT_EQ(probe.waits, 1U);
}

TEST(LlvmLibcAioServiceRelativeTest, NormalizesCarryAndAcceptsExactMaximum) {
  const time_t max = numeric_limits<time_t>::max();
  struct Case {
    timespec now, interval, expected;
  };
  const Case cases[] = {
      {{10, 900000000}, {0, 0}, {10, 900000000}},
      {{10, 900000000}, {2, 99999999}, {12, 999999999}},
      {{10, 900000000}, {2, 100000000}, {13, 0}},
      {{10, 999999999}, {2, 999999999}, {13, 999999998}},
      {{0, 0}, {max, 999999999}, {max, 999999999}},
      {{max - 1, 999999999}, {0, 1}, {max, 0}},
      {{max - 1, 0}, {1, 999999999}, {max, 999999999}}};
  for (const auto &entry : cases) {
    Probe probe;
    probe.now = entry.now;
    probe.expected = entry.expected;
    EXPECT_EQ(probe.run(&entry.interval), EAGAIN);
    EXPECT_EQ(probe.clocks, 1U);
    EXPECT_EQ(probe.waits, 1U);
  }
}

TEST(LlvmLibcAioServiceRelativeTest, OverflowIsPrivateRangeErrorNotTimeout) {
  const time_t max = numeric_limits<time_t>::max();
  const timespec intervals[] = {{1, 0}, {0, 1}, {max, 999999999}};
  for (const auto &interval : intervals) {
    Probe probe;
    probe.now = {max, 999999999};
    EXPECT_EQ(probe.run(&interval), ERANGE);
    EXPECT_EQ(probe.clocks, 1U);
    EXPECT_EQ(probe.waits, 0U);
    Probe ready;
    ready.now = probe.now;
    ready.ready_at_clock = true;
    EXPECT_EQ(ready.run(&interval), 0);
    EXPECT_EQ(ready.waits, 0U);
  }
}

TEST(LlvmLibcAioServiceRelativeTest, ClockErrorsRecheckCompletion) {
  const timespec interval{0, 1};
  const int errors[] = {EIO, EINTR, ENOSYS};
  for (int error : errors) {
    for (bool ready : {false, true}) {
      Probe probe;
      probe.clock_error = error;
      probe.ready_at_clock = ready;
      EXPECT_EQ(probe.run(&interval), ready ? 0 : error);
      EXPECT_EQ(probe.clocks, 1U);
      EXPECT_EQ(probe.waits, 0U);
    }
  }
  const timespec malformed[] = {{-1, 0}, {0, -1}, {0, 1000000000}};
  for (const auto &now : malformed) {
    for (bool ready : {false, true}) {
      Probe probe;
      probe.now = now;
      probe.ready_at_clock = ready;
      EXPECT_EQ(probe.run(&interval), ready ? 0 : EPROTO);
      EXPECT_EQ(probe.waits, 0U);
    }
  }
}

TEST(LlvmLibcAioServiceRelativeTest, RetriesNeitherResampleNorRecopyInterval) {
  Probe probe;
  timespec interval{2, 100000000};
  probe.expected = {13, 0};
  probe.retries = 4;
  probe.mutate = &interval;
  EXPECT_EQ(probe.run(&interval), EAGAIN);
  EXPECT_EQ(probe.clocks, 1U);
  EXPECT_EQ(probe.waits, 5U);
}

TEST(LlvmLibcAioServiceRelativeTest, CompletionWinsWaitErrorsAndEintrStops) {
  const timespec interval{2, 100000000};
  const int errors[] = {EINTR, ETIMEDOUT, EIO};
  for (int error : errors) {
    for (bool ready : {false, true}) {
      Probe probe;
      probe.expected = {13, 0};
      probe.result = -error;
      probe.ready_at_wait = ready;
      EXPECT_EQ(probe.run(&interval),
                ready ? 0 : error == ETIMEDOUT ? EAGAIN : error);
      EXPECT_EQ(probe.clocks, 1U);
      EXPECT_EQ(probe.waits, 1U);
    }
  }
}
