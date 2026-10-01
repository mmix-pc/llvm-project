//===-- Test expired condition waits -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/threads/linux/futex_word.h"
#include <linux/futex.h>

namespace LIBC_NAMESPACE_DECL {
static void (*observe_wait)() = nullptr;

static bool is_futex_wait(...) { return false; }
template <typename Address, typename... Args>
bool is_futex_wait(Address, uint32_t operation, Args...) {
  return (operation & FUTEX_CMD_MASK) == FUTEX_WAIT_BITSET;
}

// Observe the real wait boundary without relying on a contender's scheduling.
// All syscalls still execute normally; only this test's inline helpers use it.
template <typename Ret, typename... Args>
Ret observed_syscall(long number, Args... args) {
  if (number == FUTEX_SYSCALL_ID && is_futex_wait(args...) && observe_wait) {
    auto callback = observe_wait;
    observe_wait = nullptr;
    callback();
  }
  return syscall_impl<Ret>(number, args...);
}
} // namespace LIBC_NAMESPACE_DECL

#define syscall_impl observed_syscall
#include "src/pthread/pthread_cond_utils.h"
#undef syscall_impl
#include "test/IntegrationTest/test.h"

using namespace LIBC_NAMESPACE;
static Mutex *waiting_mutex;
static bool observed;
static void check_unlocked() {
  ASSERT_EQ(waiting_mutex->try_lock(), MutexError::NONE);
  ASSERT_EQ(waiting_mutex->unlock(), MutexError::NONE);
  observed = true;
}

TEST_MAIN() {
  for (int clock = 0; clock != 2; ++clock) {
    Mutex mutex{false, false, false, false};
    CndVar cond{false, bool(clock)};
    waiting_mutex = &mutex;
    const timespec deadlines[] = {{-1, 0}, {-1, 999999999}, {0, 0}};
    for (auto deadline : deadlines) {
      ASSERT_EQ(mutex.lock(), MutexError::NONE);
      observed = false;
      observe_wait = check_unlocked;
      ASSERT_EQ(pthread_cond_utils::timed_wait(&cond, &mutex, &deadline, clock),
                ETIMEDOUT);
      observe_wait = nullptr;
      ASSERT_TRUE(observed);
      ASSERT_EQ(mutex.try_lock(), MutexError::BUSY);
      ASSERT_EQ(mutex.unlock(), MutexError::NONE);
    }
    timespec invalid{-1, -1};
    ASSERT_EQ(mutex.lock(), MutexError::NONE);
    observed = false;
    observe_wait = check_unlocked;
    ASSERT_EQ(pthread_cond_utils::timed_wait(&cond, &mutex, &invalid, clock),
              EINVAL);
    observe_wait = nullptr;
    ASSERT_FALSE(observed);
    ASSERT_EQ(mutex.try_lock(), MutexError::BUSY);
    ASSERT_EQ(mutex.unlock(), MutexError::NONE);
  }
  return 0;
}
