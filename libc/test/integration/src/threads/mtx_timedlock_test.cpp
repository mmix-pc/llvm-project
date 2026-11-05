//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/mtx_destroy.h"
#include "src/threads/mtx_init.h"
#include "src/threads/mtx_timedlock.h"
#include "src/threads/mtx_trylock.h"
#include "src/threads/mtx_unlock.h"
#include "src/threads/thrd_create.h"
#include "src/threads/thrd_join.h"
#include "test/IntegrationTest/test.h"

#include <threads.h>
#include <time.h>

static mtx_t mutex;

static int contend(void *) {
  ASSERT_EQ(LIBC_NAMESPACE::mtx_trylock(&mutex), int(thrd_busy));
  timespec past{-1, 0};
  ASSERT_EQ(LIBC_NAMESPACE::mtx_timedlock(&mutex, &past), int(thrd_timedout));
  timespec invalid{0, 1000000000};
  ASSERT_EQ(LIBC_NAMESPACE::mtx_timedlock(&mutex, &invalid), int(thrd_error));
  return 17;
}

TEST_MAIN() {
  const int types[] = {mtx_timed, mtx_timed | mtx_recursive};
  for (int type : types) {
    ASSERT_EQ(LIBC_NAMESPACE::mtx_init(&mutex, type), int(thrd_success));
    timespec invalid{0, 1000000000};
    ASSERT_EQ(LIBC_NAMESPACE::mtx_timedlock(&mutex, &invalid),
              int(thrd_success));
    if (type & mtx_recursive) {
      ASSERT_EQ(LIBC_NAMESPACE::mtx_timedlock(&mutex, &invalid),
                int(thrd_success));
      ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&mutex), int(thrd_success));
    }
    thrd_t thread;
    ASSERT_EQ(LIBC_NAMESPACE::thrd_create(&thread, contend, nullptr),
              int(thrd_success));
    int result;
    ASSERT_EQ(LIBC_NAMESPACE::thrd_join(thread, &result), int(thrd_success));
    ASSERT_EQ(result, 17);
    ASSERT_EQ(LIBC_NAMESPACE::mtx_unlock(&mutex), int(thrd_success));
    LIBC_NAMESPACE::mtx_destroy(&mutex);
  }
  return 0;
}
