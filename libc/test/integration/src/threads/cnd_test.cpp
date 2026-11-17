//===-- Tests for standard condition variables ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/cnd_broadcast.h"
#include "src/threads/cnd_destroy.h"
#include "src/threads/cnd_init.h"
#include "src/threads/cnd_signal.h"
#include "src/threads/cnd_timedwait.h"
#include "src/threads/cnd_wait.h"
#include "src/threads/mtx_destroy.h"
#include "src/threads/mtx_init.h"
#include "src/threads/mtx_lock.h"
#include "src/threads/mtx_unlock.h"
#include "src/threads/thrd_create.h"
#include "src/threads/thrd_join.h"
#include "src/time/timespec_get.h"

#include "test/IntegrationTest/test.h"

#include <threads.h>
#include <time.h> // for TIME_UTC

static void add_ns(struct timespec &ts, long ns) {
  ts.tv_nsec += ns;
  if (ts.tv_nsec >= 1'000'000'000) {
    ++ts.tv_sec;
    ts.tv_nsec -= 1'000'000'000;
  }
}

namespace wait_notify_broadcast_test {

// Keep this a bounded functional test. Both readiness and release predicates
// are protected by the same mutex, including across spurious wakeups.
constexpr unsigned int THRD_COUNT = 4;

static unsigned int ready_count, completed_count;
static bool released;
static cnd_t broadcast_cnd, threads_ready_cnd;
static mtx_t broadcast_mtx;

int broadcast_thread_func(void *) {
  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&broadcast_mtx), int(thrd_success));
  if (++ready_count == THRD_COUNT)
    ASSERT_EQ(LIBC_NAMESPACE::cnd_signal(&threads_ready_cnd),
              int(thrd_success));
  while (!released)
    ASSERT_EQ(LIBC_NAMESPACE::cnd_wait(&broadcast_cnd, &broadcast_mtx),
              int(thrd_success));
  ++completed_count;
  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&broadcast_mtx), int(thrd_success));
  return 0;
}

void wait_notify_broadcast_test() {
  ready_count = completed_count = 0;
  released = false;
  ASSERT_EQ(LIBC_NAMESPACE::cnd_init(&broadcast_cnd), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::cnd_init(&threads_ready_cnd), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_init(&broadcast_mtx, mtx_plain),
            int(thrd_success));

  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&broadcast_mtx), int(thrd_success));
  thrd_t threads[THRD_COUNT];
  for (unsigned int i = 0; i < THRD_COUNT; ++i)
    ASSERT_EQ(LIBC_NAMESPACE::thrd_create(&threads[i], broadcast_thread_func,
                                          nullptr),
              int(thrd_success));

  while (ready_count != THRD_COUNT)
    ASSERT_EQ(LIBC_NAMESPACE::cnd_wait(&threads_ready_cnd, &broadcast_mtx),
              int(thrd_success));
  ASSERT_EQ(completed_count, 0U);
  released = true;
  ASSERT_EQ(LIBC_NAMESPACE::cnd_broadcast(&broadcast_cnd), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&broadcast_mtx), int(thrd_success));

  for (unsigned int i = 0; i < THRD_COUNT; ++i) {
    int retval = 0xBAD;
    ASSERT_EQ(LIBC_NAMESPACE::thrd_join(threads[i], &retval),
              int(thrd_success));
    ASSERT_EQ(retval, 0);
  }
  ASSERT_EQ(completed_count, THRD_COUNT);

  LIBC_NAMESPACE::cnd_destroy(&broadcast_cnd);
  LIBC_NAMESPACE::cnd_destroy(&threads_ready_cnd);
  LIBC_NAMESPACE::mtx_destroy(&broadcast_mtx);
}

} // namespace wait_notify_broadcast_test

namespace single_waiter_test {

// In this namespace we set up test with two threads, one the main thread
// and the other a waiter thread. They wait on each other using condition
// variables and mutexes before proceeding to completion.

mtx_t mtx;
cnd_t waiter_cnd, main_thread_cnd;
bool waiter_ready, released;

enum class WaitMode {
  Default,
  Timed,
};

int waiter_thread_func(void *) {
  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&mtx), int(thrd_success));
  waiter_ready = true;
  ASSERT_EQ(LIBC_NAMESPACE::cnd_signal(&main_thread_cnd), int(thrd_success));
  while (!released)
    ASSERT_EQ(LIBC_NAMESPACE::cnd_wait(&waiter_cnd, &mtx), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&mtx), int(thrd_success));
  return 0x600D;
}

void single_waiter_test(WaitMode wait_mode) {
  waiter_ready = released = false;
  ASSERT_EQ(LIBC_NAMESPACE::mtx_init(&mtx, mtx_plain), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::cnd_init(&waiter_cnd), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::cnd_init(&main_thread_cnd), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&mtx), int(thrd_success));

  thrd_t waiter_thread;
  ASSERT_EQ(
      LIBC_NAMESPACE::thrd_create(&waiter_thread, waiter_thread_func, nullptr),
      int(thrd_success));
  struct timespec ts;
  if (wait_mode == WaitMode::Timed) {
    ASSERT_EQ(LIBC_NAMESPACE::timespec_get(&ts, TIME_UTC), TIME_UTC);
    add_ns(ts, 50'000);
  }
  while (!waiter_ready) {
    if (wait_mode == WaitMode::Default) {
      ASSERT_EQ(LIBC_NAMESPACE::cnd_wait(&main_thread_cnd, &mtx),
                int(thrd_success));
    } else {
      int result = LIBC_NAMESPACE::cnd_timedwait(&main_thread_cnd, &mtx, &ts);
      ASSERT_TRUE(result == int(thrd_success) || result == int(thrd_timedout));
      if (result == int(thrd_timedout)) {
        // The short deadline need not outlast thread startup. Finish the
        // readiness handshake without repeatedly waiting on an expired time.
        while (!waiter_ready)
          ASSERT_EQ(LIBC_NAMESPACE::cnd_wait(&main_thread_cnd, &mtx),
                    int(thrd_success));
      }
    }
  }
  released = true;
  ASSERT_EQ(LIBC_NAMESPACE::cnd_signal(&waiter_cnd), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&mtx), int(thrd_success));

  int retval;
  ASSERT_EQ(LIBC_NAMESPACE::thrd_join(waiter_thread, &retval),
            int(thrd_success));
  ASSERT_EQ(retval, 0x600D);
  LIBC_NAMESPACE::mtx_destroy(&mtx);
  LIBC_NAMESPACE::cnd_destroy(&waiter_cnd);
  LIBC_NAMESPACE::cnd_destroy(&main_thread_cnd);
}

} // namespace single_waiter_test

namespace timed_wait_test {

void timeout_test() {
  cnd_t cnd;
  mtx_t mtx;
  ASSERT_EQ(LIBC_NAMESPACE::cnd_init(&cnd), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_init(&mtx, mtx_plain), int(thrd_success));

  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&mtx), int(thrd_success));

  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 0;
  // A successful spurious wakeup does not discharge the timeout observation.
  int result;
  do {
    result = LIBC_NAMESPACE::cnd_timedwait(&cnd, &mtx, &ts);
    ASSERT_TRUE(result == int(thrd_success) || result == int(thrd_timedout));
  } while (result != int(thrd_timedout));

  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&mtx), int(thrd_success));

  LIBC_NAMESPACE::cnd_destroy(&cnd);
  LIBC_NAMESPACE::mtx_destroy(&mtx);
}

void future_timeout_test() {
  cnd_t cnd;
  mtx_t mtx;
  ASSERT_EQ(LIBC_NAMESPACE::cnd_init(&cnd), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_init(&mtx, mtx_plain), int(thrd_success));

  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&mtx), int(thrd_success));

  struct timespec ts;
  ASSERT_EQ(LIBC_NAMESPACE::timespec_get(&ts, TIME_UTC), TIME_UTC);
  add_ns(ts, 50'000);
  // A successful spurious wakeup does not discharge the timeout observation.
  int result;
  do {
    result = LIBC_NAMESPACE::cnd_timedwait(&cnd, &mtx, &ts);
    ASSERT_TRUE(result == int(thrd_success) || result == int(thrd_timedout));
  } while (result != int(thrd_timedout));

  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&mtx), int(thrd_success));

  LIBC_NAMESPACE::cnd_destroy(&cnd);
  LIBC_NAMESPACE::mtx_destroy(&mtx);
}

} // namespace timed_wait_test

TEST_MAIN() {
  wait_notify_broadcast_test::wait_notify_broadcast_test();
  single_waiter_test::single_waiter_test(single_waiter_test::WaitMode::Default);
  single_waiter_test::single_waiter_test(single_waiter_test::WaitMode::Timed);
  timed_wait_test::timeout_test();
  timed_wait_test::future_timeout_test();
  return 0;
}
