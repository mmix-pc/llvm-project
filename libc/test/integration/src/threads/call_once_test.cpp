//===-- Tests for call_once -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/atomic.h"
#include "src/threads/call_once.h"
#include "src/threads/mtx_destroy.h"
#include "src/threads/mtx_init.h"
#include "src/threads/mtx_lock.h"
#include "src/threads/mtx_unlock.h"
#include "src/threads/thrd_create.h"
#include "src/threads/thrd_join.h"

#include "test/IntegrationTest/test.h"

#include <threads.h>

static constexpr unsigned int NUM_THREADS = 5;
static LIBC_NAMESPACE::cpp::Atomic<unsigned int> thread_count;

static unsigned int call_count;
static void call_once_func() { ++call_count; }

static int func(void *) {
  static once_flag flag = ONCE_FLAG_INIT;
  LIBC_NAMESPACE::call_once(&flag, call_once_func);

  thread_count.fetch_add(1);

  return 0;
}

void call_from_5_threads() {
  // Ensure the call count and thread count are 0 to begin with.
  call_count = 0;
  thread_count = 0;

  thrd_t threads[NUM_THREADS];
  for (unsigned int i = 0; i < NUM_THREADS; ++i) {
    ASSERT_EQ(LIBC_NAMESPACE::thrd_create(threads + i, func, nullptr),
              static_cast<int>(thrd_success));
  }

  for (unsigned int i = 0; i < NUM_THREADS; ++i) {
    int retval;
    ASSERT_EQ(LIBC_NAMESPACE::thrd_join(threads[i], &retval),
              static_cast<int>(thrd_success));
    ASSERT_EQ(retval, 0);
  }

  ASSERT_EQ(thread_count.load(), NUM_THREADS);
  ASSERT_EQ(call_count, 1U);
}

static mtx_t once_func_blocker;
static LIBC_NAMESPACE::cpp::Atomic<bool> initializer_entered(false);
static unsigned int initialized_value;
static LIBC_NAMESPACE::cpp::Atomic<unsigned int> initializer_count(0);
static void blocking_once_func() {
  initializer_count.fetch_add(1);
  initializer_entered.store(true);
  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&once_func_blocker), int(thrd_success));
  initialized_value = 0x600D;
  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&once_func_blocker), int(thrd_success));
}

static LIBC_NAMESPACE::cpp::Atomic<unsigned int> start_count;
static LIBC_NAMESPACE::cpp::Atomic<unsigned int> done_count;
static int once_func_caller(void *) {
  static once_flag flag = ONCE_FLAG_INIT;
  start_count.fetch_add(1);
  LIBC_NAMESPACE::call_once(&flag, blocking_once_func);
  // This ordinary read must observe initialization through call_once, not a
  // separate acquire of the initializer's mutex or completion flag.
  ASSERT_EQ(initialized_value, 0x600DU);
  done_count.fetch_add(1);
  return 0;
}

// Observe initializer entry and verify publication to both returning callers.
// A caller's start counter does not prove it has entered call_once; this test
// makes no claim to force a particular internal waiter state.
void test_synchronization() {
  start_count = 0;
  done_count = 0;
  initializer_entered.store(false);
  initialized_value = 0;
  initializer_count.store(0);

  ASSERT_EQ(LIBC_NAMESPACE::mtx_init(&once_func_blocker, mtx_plain),
            static_cast<int>(thrd_success));
  // Lock the blocking mutex so that the once func blocks.
  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&once_func_blocker),
            static_cast<int>(thrd_success));

  thrd_t t1, t2;
  ASSERT_EQ(LIBC_NAMESPACE::thrd_create(&t1, once_func_caller, nullptr),
            static_cast<int>(thrd_success));
  while (!initializer_entered.load())
    ; // The first caller is inside the initializer, held by the mutex.
  ASSERT_EQ(LIBC_NAMESPACE::thrd_create(&t2, once_func_caller, nullptr),
            static_cast<int>(thrd_success));

  while (start_count.load() != 2)
    ; // Spin until both threads start.

  // Since the once func is blocked, the threads should not be done yet.
  ASSERT_EQ(done_count.load(), 0U);

  // Release the initializer so both callers can complete.
  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&once_func_blocker),
            static_cast<int>(thrd_success));

  int retval;
  ASSERT_EQ(LIBC_NAMESPACE::thrd_join(t1, &retval),
            static_cast<int>(thrd_success));
  ASSERT_EQ(retval, 0);
  ASSERT_EQ(LIBC_NAMESPACE::thrd_join(t2, &retval),
            static_cast<int>(thrd_success));
  ASSERT_EQ(retval, 0);

  ASSERT_EQ(done_count.load(), 2U);
  ASSERT_EQ(initializer_count.load(), 1U);

  LIBC_NAMESPACE::mtx_destroy(&once_func_blocker);
}

TEST_MAIN() {
  call_from_5_threads();
  test_synchronization();
  return 0;
}
