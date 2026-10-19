//===-- Tests for private mutex fork ownership ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/unix_mutex.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Mutex;
using LIBC_NAMESPACE::MutexError;

TEST(LlvmLibcForkOwnerTest, RecursiveDepth) {
  Mutex mutex(false, true, false, false);
  const pid_t current = LIBC_NAMESPACE::internal::gettid();
  const pid_t previous = current == 1 ? 2 : 1;
  ASSERT_EQ(mutex.lock(), MutexError::NONE);
  ASSERT_EQ(mutex.lock(), MutexError::NONE);
  EXPECT_FALSE(mutex.rebind_owner_after_fork(previous, current));
  EXPECT_FALSE(mutex.rebind_owner_after_fork(current, 0));
  ASSERT_TRUE(mutex.rebind_owner_after_fork(current, previous));
  EXPECT_EQ(mutex.unlock(), MutexError::UNLOCK_WITHOUT_LOCK);
  ASSERT_TRUE(mutex.rebind_owner_after_fork(previous, current));
  EXPECT_EQ(mutex.unlock(), MutexError::NONE);
  EXPECT_EQ(mutex.unlock(), MutexError::NONE);
  EXPECT_EQ(mutex.unlock(), MutexError::UNLOCK_WITHOUT_LOCK);
  EXPECT_FALSE(mutex.rebind_owner_after_fork(current, previous));
}

TEST(LlvmLibcForkOwnerTest, RejectUnrelatedLocks) {
  const pid_t current = LIBC_NAMESPACE::internal::gettid();
  Mutex shared(false, true, false, true);
  Mutex robust(false, true, true, false);
  Mutex plain(false, false, false, false);
  Mutex *locks[] = {&shared, &robust, &plain};
  for (Mutex *mutex : locks) {
    ASSERT_EQ(mutex->lock(), MutexError::NONE);
    EXPECT_FALSE(mutex->rebind_owner_after_fork(current, current));
    EXPECT_EQ(mutex->unlock(), MutexError::NONE);
  }
}
