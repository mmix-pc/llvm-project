//===-- Unittests for RawRwLock -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/time_macros.h"
#include "src/__support/threads/raw_rwlock.h"
#include "src/__support/time/clock_gettime.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
namespace rwlock {
class RwLockTester {
public:
  template <Role role> static void expired_slow_entry(bool shared) {
    RawRwLock lock(Role::Reader, shared);
    auto timeout = internal::AbsTimeout::from_timespec({0, 0}, true);
    ASSERT_TRUE(timeout.has_value());
    unsigned checks = 0;
    auto expired = [&]() {
      ++checks;
      return true;
    };
    // Model the wrapper's already-Busy boundary with a now-available lock.
    EXPECT_EQ(lock.lock_slow<role>(*timeout, 0, expired), LockResult::TimedOut);
    EXPECT_EQ(checks, 1u);
    EXPECT_EQ(lock.try_write_lock(), LockResult::Success);
    EXPECT_EQ(lock.unlock(), LockResult::Success);
    checks = 0;
    EXPECT_EQ(lock.lock_slow<role>(cpp::nullopt, 0, expired),
              LockResult::Success);
    EXPECT_EQ(checks, 0u);
    EXPECT_EQ(lock.unlock(), LockResult::Success);
  }

  template <Role role> static void expired_retry_publishes_waiters(bool shared) {
    RawRwLock lock(Role::Reader, shared);
    ASSERT_EQ(lock.try_write_lock(), LockResult::Success);
    {
      auto guard = lock.queue.acquire(shared);
      guard.pending_count<Role::Reader>() = 1;
      guard.pending_count<Role::Writer>() = 1;
      RwState::fetch_set_pending_bit<Role::Reader>(lock.state,
                                                  cpp::MemoryOrder::RELAXED);
      RwState::fetch_set_pending_bit<Role::Writer>(lock.state,
                                                  cpp::MemoryOrder::RELAXED);
    }
    auto timeout = internal::AbsTimeout::from_timespec({0, 0}, true);
    ASSERT_TRUE(timeout.has_value());
    unsigned checks = 0;
    auto expired = [&]() {
      ++checks;
      // Release after the slow path sampled the busy state. Registration then
      // sees the free lock and skips the kernel wait. Expire only after the
      // post-wait check and queue withdrawal, immediately before retry.
      if (checks == 1)
        EXPECT_EQ(lock.unlock(), LockResult::Success);
      return checks >= 3;
    };
    EXPECT_EQ(lock.lock_slow<role>(*timeout, 0, expired), LockResult::TimedOut);
    EXPECT_EQ(checks, 3u);
    {
      auto guard = lock.queue.acquire(shared);
      EXPECT_EQ(guard.pending_count<Role::Reader>(), 1u);
      EXPECT_EQ(guard.pending_count<Role::Writer>(), 1u);
      EXPECT_EQ(guard.serialization<Role::Reader>().load(), 2u);
      EXPECT_EQ(guard.serialization<Role::Writer>().load(), 2u);
      EXPECT_TRUE(!RwState::load(lock.state, cpp::MemoryOrder::RELAXED)
                       .has_active_owner());
      guard.pending_count<Role::Reader>() = 0;
      guard.pending_count<Role::Writer>() = 0;
      lock.state.store(0);
    }
    EXPECT_EQ(lock.try_write_lock(), LockResult::Success);
    EXPECT_EQ(lock.unlock(), LockResult::Success);
  }

  static void handoff(bool shared) {
    static_assert(sizeof(Futex) == sizeof(cpp::Atomic<int>));
    static_assert(alignof(Futex) == alignof(cpp::Atomic<int>));
    RawRwLock lock(Role::Reader, shared);
    ASSERT_EQ(lock.read_lock(), LockResult::Success);
    auto old = RwState::load(lock.state, cpp::MemoryOrder::RELAXED);
    lock.state.store(int(old.unlock_handoff()));
    EXPECT_TRUE(RwState::load(lock.state, cpp::MemoryOrder::RELAXED)
                    .is_unlock_handoff());
    EXPECT_EQ(lock.try_read_lock(), LockResult::Busy);
    EXPECT_EQ(lock.try_write_lock(), LockResult::Busy);
    lock.notify_pending_threads();
    EXPECT_EQ(lock.try_write_lock(), LockResult::Success);
    EXPECT_EQ(lock.unlock(), LockResult::Success);
    {
      auto guard = lock.queue.acquire(shared);
      EXPECT_EQ(guard.serialization<Role::Reader>().load(), 1u);
      EXPECT_EQ(guard.serialization<Role::Writer>().load(), 1u);
    }
    // Handoff uses an otherwise impossible writer+reader combination, not a
    // bit taken from the ordinary reader count.
    const int maximum = cpp::numeric_limits<int>::max() & ~3;
    lock.state.store(maximum);
    EXPECT_EQ(lock.try_read_lock(), LockResult::Overflow);
    EXPECT_EQ(int(lock.state.load()), maximum);
    lock.state.store(0);
    EXPECT_EQ(lock.check_for_destroy(), LockResult::Success);
  }

  static void last_writer_timeout(bool shared, bool another_writer) {
    RawRwLock lock(Role::Writer, shared);
    ASSERT_EQ(lock.read_lock(), LockResult::Success);
    FutexWordType serial;
    {
      auto guard = lock.queue.acquire(shared);
      // Model a reader already asleep on the queue serialization word. This
      // makes notification publication observable without scheduling a thread.
      guard.pending_count<Role::Reader>() = 1;
      RwState::fetch_set_pending_bit<Role::Reader>(lock.state,
                                                   cpp::MemoryOrder::RELAXED);
      if (another_writer) {
        guard.pending_count<Role::Writer>() = 1;
        RwState::fetch_set_pending_bit<Role::Writer>(lock.state,
                                                     cpp::MemoryOrder::RELAXED);
      }
      serial = guard.serialization<Role::Reader>().load();
    }
    auto expired = internal::AbsTimeout::from_timespec({0, 0}, true);
    ASSERT_TRUE(expired.has_value());
    EXPECT_EQ(lock.write_lock(*expired, 0), LockResult::TimedOut);
    {
      auto guard = lock.queue.acquire(shared);
      EXPECT_EQ(guard.serialization<Role::Reader>().load(),
                serial + (another_writer ? 0u : 1u));
      EXPECT_EQ(guard.pending_count<Role::Writer>(), another_writer ? 1u : 0u);
      EXPECT_EQ(guard.pending_count<Role::Reader>(), 1u);
      EXPECT_EQ(RwState::load(lock.state, cpp::MemoryOrder::RELAXED)
                    .has_pending_writer(),
                another_writer);
      EXPECT_EQ(lock.try_read_lock(),
                another_writer ? LockResult::Busy : LockResult::Success);
      if (!another_writer)
        EXPECT_EQ(lock.unlock(), LockResult::Success);
      guard.pending_count<Role::Reader>() = 0;
      guard.pending_count<Role::Writer>() = 0;
      RwState::fetch_clear_pending_bit<Role::Reader>(lock.state,
                                                     cpp::MemoryOrder::RELAXED);
      RwState::fetch_clear_pending_bit<Role::Writer>(lock.state,
                                                     cpp::MemoryOrder::RELAXED);
    }
    EXPECT_EQ(lock.unlock(), LockResult::Success);
  }
};
} // namespace rwlock
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcSupportThreadsRawRwLockTest, PostBusyDeadlinePrecedesAcquisition) {
  using Tester = LIBC_NAMESPACE::rwlock::RwLockTester;
  using Role = LIBC_NAMESPACE::rwlock::Role;
  const bool values[] = {false, true};
  for (bool shared : values) {
    Tester::expired_slow_entry<Role::Reader>(shared);
    Tester::expired_slow_entry<Role::Writer>(shared);
  }
}

TEST(LlvmLibcSupportThreadsRawRwLockTest, ExpiredRetryPublishesReplacementWake) {
  using Tester = LIBC_NAMESPACE::rwlock::RwLockTester;
  using Role = LIBC_NAMESPACE::rwlock::Role;
  const bool values[] = {false, true};
  for (bool shared : values) {
    Tester::expired_retry_publishes_waiters<Role::Reader>(shared);
    Tester::expired_retry_publishes_waiters<Role::Writer>(shared);
  }
}

TEST(LlvmLibcSupportThreadsRawRwLockTest, HandoffRetainsOwnership) {
  LIBC_NAMESPACE::rwlock::RwLockTester::handoff(false);
  LIBC_NAMESPACE::rwlock::RwLockTester::handoff(true);
}

TEST(LlvmLibcSupportThreadsRawRwLockTest, LastWriterTimeoutPublishesReaders) {
  const bool values[] = {false, true};
  for (bool shared : values)
    for (bool another_writer : values)
      LIBC_NAMESPACE::rwlock::RwLockTester::last_writer_timeout(shared,
                                                                another_writer);
}

TEST(LlvmLibcSupportThreadsRawRwLockTest, SmokeTest) {
  LIBC_NAMESPACE::RawRwLock rwlock;
  using LockResult = LIBC_NAMESPACE::RawRwLock::LockResult;

  ASSERT_EQ(rwlock.read_lock(), LockResult::Success);
  ASSERT_EQ(rwlock.try_read_lock(), LockResult::Success);
  ASSERT_EQ(rwlock.try_write_lock(), LockResult::Busy);
  ASSERT_EQ(rwlock.unlock(), LockResult::Success);
  ASSERT_EQ(rwlock.unlock(), LockResult::Success);

  ASSERT_EQ(rwlock.write_lock(), LockResult::Success);
  ASSERT_EQ(rwlock.try_read_lock(), LockResult::Busy);
  ASSERT_EQ(rwlock.try_write_lock(), LockResult::Busy);
  ASSERT_EQ(rwlock.unlock(), LockResult::Success);
  ASSERT_EQ(rwlock.unlock(), LockResult::PermissionDenied);
}

TEST(LlvmLibcSupportThreadsRawRwLockTest, TimeoutWithoutDeadlockDetection) {
  LIBC_NAMESPACE::RawRwLock rwlock;
  using LockResult = LIBC_NAMESPACE::RawRwLock::LockResult;

  ASSERT_EQ(rwlock.write_lock(), LockResult::Success);

  timespec ts;
  LIBC_NAMESPACE::internal::clock_gettime(CLOCK_MONOTONIC, &ts);
  ts.tv_sec += 1;
  auto timeout = LIBC_NAMESPACE::internal::AbsTimeout::from_timespec(
      ts, /*is_realtime=*/false);
  ASSERT_TRUE(timeout.has_value());

  ASSERT_EQ(rwlock.write_lock(*timeout), LockResult::TimedOut);
  ASSERT_EQ(rwlock.read_lock(*timeout), LockResult::TimedOut);
  ASSERT_EQ(rwlock.unlock(), LockResult::Success);
}

TEST(LlvmLibcSupportThreadsRawRwLockTest, ImmediateAcquisitionSkipsDeadlineCheck) {
  LIBC_NAMESPACE::RawRwLock rwlock;
  using LockResult = LIBC_NAMESPACE::RawRwLock::LockResult;
  auto timeout = LIBC_NAMESPACE::internal::AbsTimeout::from_timespec({0, 0}, true);
  ASSERT_TRUE(timeout.has_value());
  unsigned checks = 0;
  auto expired = [&]() {
    ++checks;
    return true;
  };
  ASSERT_EQ(rwlock.read_lock(*timeout, 0, expired), LockResult::Success);
  ASSERT_EQ(checks, 0u);
  ASSERT_EQ(rwlock.unlock(), LockResult::Success);
  ASSERT_EQ(rwlock.write_lock(*timeout, 0, expired), LockResult::Success);
  ASSERT_EQ(checks, 0u);
  ASSERT_EQ(rwlock.unlock(), LockResult::Success);
}
