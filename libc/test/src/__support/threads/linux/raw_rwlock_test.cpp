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
