//===-- Semaphore signal interruption and signal-safe posting -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/time_macros.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/libc_errno.h"
#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#include "src/semaphore/sem_destroy.h"
#include "src/semaphore/sem_getvalue.h"
#include "src/semaphore/sem_init.h"
#include "src/semaphore/sem_post.h"
#include "src/semaphore/sem_timedwait.h"
#include "src/semaphore/sem_wait.h"
#include "src/signal/pthread_kill.h"
#include "src/signal/sigaction.h"
#include "src/signal/sigemptyset.h"
#include "src/time/clock_gettime.h"
#include "src/time/nanosleep.h"
#include "test/IntegrationTest/test.h"

namespace LIBC = LIBC_NAMESPACE;
namespace {
sem_t semaphore;
LIBC::cpp::Atomic<unsigned> ready{0}, done{0};
bool timed, post;
volatile sig_atomic_t handler_error;

void handler(int) {
  if (post && LIBC::sem_post(&semaphore) != 0)
    handler_error = 1;
}

void *waiter(void *) {
  timespec deadline;
  ASSERT_EQ(LIBC::clock_gettime(CLOCK_REALTIME, &deadline), 0);
  deadline.tv_sec += 5;
  ready.store(1);
  int result;
  do {
    result = timed ? LIBC::sem_timedwait(&semaphore, &deadline)
                   : LIBC::sem_wait(&semaphore);
  } while (post && result == -1 && LIBC::libc_errno == EINTR);
  if (post) {
    ASSERT_EQ(result, 0);
  } else {
    ASSERT_EQ(result, -1);
    ASSERT_EQ(LIBC::libc_errno, EINTR);
  }
  done.store(1);
  return nullptr;
}
} // namespace

TEST_MAIN() {
  struct sigaction action{}, previous{};
  action.sa_handler = handler;
  ASSERT_EQ(LIBC::sigemptyset(&action.sa_mask), 0);
  ASSERT_EQ(LIBC::sigaction(SIGUSR1, &action, &previous), 0);
  for (unsigned mode = 0; mode != 8; ++mode) {
    timed = mode & 1;
    post = mode & 2;
    handler_error = 0;
    ready.store(0);
    done.store(0);
    ASSERT_EQ(LIBC::sem_init(&semaphore, (mode >> 2) & 1, 0), 0);
    pthread_t thread;
    ASSERT_EQ(LIBC::pthread_create(&thread, nullptr, waiter, nullptr), 0);
    while (!ready.load()) {
    }
    timespec deadline, now, pause{0, 1000000};
    ASSERT_EQ(LIBC::clock_gettime(CLOCK_MONOTONIC, &deadline), 0);
    deadline.tv_sec += 5;
    // Repeat until completion so delivery before the futex sleep cannot make
    // the interruption test pass spuriously or hang indefinitely.
    while (!done.load()) {
      int error = LIBC::pthread_kill(thread, SIGUSR1);
      ASSERT_TRUE(error == 0 || error == ESRCH);
      LIBC::nanosleep(&pause, nullptr);
      ASSERT_EQ(LIBC::clock_gettime(CLOCK_MONOTONIC, &now), 0);
      ASSERT_TRUE(
          now.tv_sec < deadline.tv_sec ||
          (now.tv_sec == deadline.tv_sec && now.tv_nsec < deadline.tv_nsec));
    }
    ASSERT_EQ(LIBC::pthread_join(thread, nullptr), 0);
    ASSERT_EQ(handler_error, 0);
    int value = -1;
    ASSERT_EQ(LIBC::sem_getvalue(&semaphore, &value), 0);
    if (!post)
      ASSERT_EQ(value, 0);
    ASSERT_EQ(LIBC::sem_destroy(&semaphore), 0);
  }
  ASSERT_EQ(LIBC::sigaction(SIGUSR1, &previous, nullptr), 0);
  return 0;
}
