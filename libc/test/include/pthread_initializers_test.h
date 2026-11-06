//===-- Tests for public pthread initializers ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Keep pthread.h as the only public include so other headers cannot supply NULL.
#include <pthread.h>

pthread_t test_null_thread = PTHREAD_NULL;
#ifdef __cplusplus
constexpr pthread_t test_constant_null_thread = PTHREAD_NULL;
static_assert(__is_same(decltype(PTHREAD_NULL), pthread_t),
              "PTHREAD_NULL must have the thread identifier type");
static_assert(sizeof(pthread_equal(PTHREAD_NULL, PTHREAD_NULL)) == sizeof(int),
              "PTHREAD_NULL must be a valid function argument");
#else
_Static_assert(_Generic(PTHREAD_NULL, pthread_t: 1, default: 0),
               "PTHREAD_NULL must have the thread identifier type");
_Static_assert(sizeof(pthread_equal(PTHREAD_NULL, PTHREAD_NULL)) == sizeof(int),
               "PTHREAD_NULL must be a valid function argument");
#endif
pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t test_condition = PTHREAD_COND_INITIALIZER;
pthread_rwlock_t test_rwlock = PTHREAD_RWLOCK_INITIALIZER;
pthread_once_t test_once = PTHREAD_ONCE_INIT;

static int check_initializers(void) {
  return test_condition.__waiter_queue[0] != 0 ||
         test_condition.__waiter_queue[1] != 0 ||
         test_condition.__futex.__word != 0 || test_condition.__is_shared != 0 ||
         test_condition.__is_realtime != 1;
}
