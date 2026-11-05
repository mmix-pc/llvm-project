//===-- Tests for expired cnd_timedwait deadlines
//--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/cnd_destroy.h"
#include "src/threads/cnd_init.h"
#include "src/threads/cnd_timedwait.h"
#include "src/threads/mtx_destroy.h"
#include "src/threads/mtx_init.h"
#include "src/threads/mtx_lock.h"
#include "src/threads/mtx_trylock.h"
#include "src/threads/mtx_unlock.h"
#include "src/threads/thrd_create.h"
#include "src/threads/thrd_join.h"

#include "test/IntegrationTest/test.h"

#include <threads.h>
#include <time.h>

namespace {

int try_lock(void *arg) {
  auto *mutex = static_cast<mtx_t *>(arg);
  int result = LIBC_NAMESPACE::mtx_trylock(mutex);
  if (result == thrd_success)
    ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(mutex), int(thrd_success));
  return result;
}

void check_lock_from_other_thread(mtx_t &mutex, int expected) {
  thrd_t thread;
  ASSERT_EQ(LIBC_NAMESPACE::thrd_create(&thread, try_lock, &mutex),
            int(thrd_success));
  int result = -1;
  ASSERT_EQ(LIBC_NAMESPACE::thrd_join(thread, &result), int(thrd_success));
  ASSERT_EQ(result, expected);
}

void check_deadline(const timespec &deadline, int expected) {
  cnd_t cond;
  mtx_t mutex;
  ASSERT_EQ(LIBC_NAMESPACE::cnd_init(&cond), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_init(&mutex, mtx_plain), int(thrd_success));
  ASSERT_EQ(LIBC_NAMESPACE::mtx_lock(&mutex), int(thrd_success));

  // Repeat on the same objects to exercise reuse after timeout cleanup.
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(LIBC_NAMESPACE::cnd_timedwait(&cond, &mutex, &deadline),
              expected);

    // This checks ownership after return, not the transient unlock during
    // the wait. An implementation that returns early with the mutex still
    // locked also passes: public APIs cannot force another thread to run
    // during that interval without a scheduling assumption or a test hook.
    check_lock_from_other_thread(mutex, int(thrd_busy));
  }

  ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&mutex), int(thrd_success));
  check_lock_from_other_thread(mutex, int(thrd_success));
  LIBC_NAMESPACE::cnd_destroy(&cond);
  LIBC_NAMESPACE::mtx_destroy(&mutex);
}

} // namespace

TEST_MAIN() {
  check_deadline({-1, 0}, int(thrd_timedout));
  check_deadline({-1, 999'999'999}, int(thrd_timedout));
  check_deadline({0, 0}, int(thrd_timedout));

  // Invalid nanoseconds take precedence over a before-epoch deadline.
  check_deadline({-1, -1}, int(thrd_error));
  check_deadline({-1, 1'000'000'000}, int(thrd_error));
  check_deadline({0, -1}, int(thrd_error));
  check_deadline({0, 1'000'000'000}, int(thrd_error));
  return 0;
}
