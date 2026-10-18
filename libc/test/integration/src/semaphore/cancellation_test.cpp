//===-- Semaphore cancellation and noncanceling shm operations ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fcntl_macros.h"
#include "hdr/semaphore_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/time_macros.h"
#include "src/__support/CPP/atomic.h"
#include "src/pthread/pthread_cancel.h"
#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#include "src/pthread/pthread_self.h"
#include "src/semaphore/sem_close.h"
#include "src/semaphore/sem_destroy.h"
#include "src/semaphore/sem_getvalue.h"
#include "src/semaphore/sem_init.h"
#include "src/semaphore/sem_open.h"
#include "src/semaphore/sem_timedwait.h"
#include "src/semaphore/sem_unlink.h"
#include "src/semaphore/sem_wait.h"
#include "src/sys/mman/shm_open.h"
#include "src/sys/mman/shm_unlink.h"
#include "src/time/clock_gettime.h"
#include "src/unistd/close.h"
#include "test/IntegrationTest/test.h"
#include <pthread.h>

namespace LIBC = LIBC_NAMESPACE;

namespace {
sem_t *semaphore;
bool timed;
bool pending;
LIBC::cpp::Atomic<unsigned> ready{0};

void *waiter(void *) {
  timespec deadline;
  ASSERT_EQ(LIBC::clock_gettime(CLOCK_REALTIME, &deadline), 0);
  deadline.tv_sec += 5;
  if (pending)
    ASSERT_EQ(LIBC::pthread_cancel(LIBC::pthread_self()), 0);
  ready.store(1);
  if (timed)
    LIBC::sem_timedwait(semaphore, &deadline);
  else
    LIBC::sem_wait(semaphore);
  // Both a pending fast-path request and a blocked request must cancel.
  return nullptr;
}

void run_wait(bool named, bool use_timeout, bool already_pending) {
  const char *name = "/llvmlibc_sem_cancel";
  sem_t local;
  unsigned count = already_pending ? 1 : 0;
  if (named) {
    LIBC::sem_unlink(name);
    semaphore = LIBC::sem_open(name, O_CREAT | O_EXCL, 0600, count);
    ASSERT_NE(semaphore, SEM_FAILED);
    ASSERT_EQ(LIBC::sem_unlink(name), 0);
  } else {
    ASSERT_EQ(LIBC::sem_init(&local, 0, count), 0);
    semaphore = &local;
  }
  timed = use_timeout;
  pending = already_pending;
  ready.store(0);
  pthread_t thread;
  ASSERT_EQ(LIBC::pthread_create(&thread, nullptr, waiter, nullptr), 0);
  if (!already_pending) {
    while (!ready.load()) {
    }
    ASSERT_EQ(LIBC::pthread_cancel(thread), 0);
  }
  void *result = nullptr;
  ASSERT_EQ(LIBC::pthread_join(thread, &result), 0);
  ASSERT_EQ(result, PTHREAD_CANCELED);
  int value = -1;
  ASSERT_EQ(LIBC::sem_getvalue(semaphore, &value), 0);
  ASSERT_EQ(value, static_cast<int>(count));
  if (named) {
    ASSERT_EQ(LIBC::sem_close(semaphore), 0);
  } else {
    ASSERT_EQ(LIBC::sem_destroy(semaphore), 0);
  }
}

void *shm_worker(void *) {
  ASSERT_EQ(LIBC::pthread_cancel(LIBC::pthread_self()), 0);
  const char *name = "/llvmlibc_shm_pending_cancel";
  int fd = LIBC::shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
  ASSERT_TRUE(fd >= 0);
  ASSERT_EQ(LIBC::shm_unlink(name), 0);
  // Transfer the descriptor to the parent: public close is a cancellation
  // point, whereas these two shared-memory operations must not be.
  return reinterpret_cast<void *>(static_cast<uintptr_t>(fd) + 1);
}
} // namespace

TEST_MAIN() {
  for (unsigned named = 0; named != 2; ++named)
    for (unsigned timeout = 0; timeout != 2; ++timeout)
      for (unsigned pending_request = 0; pending_request != 2;
           ++pending_request)
        run_wait(named, timeout, pending_request);

  LIBC::shm_unlink("/llvmlibc_shm_pending_cancel");
  pthread_t thread;
  ASSERT_EQ(LIBC::pthread_create(&thread, nullptr, shm_worker, nullptr), 0);
  void *result = nullptr;
  ASSERT_EQ(LIBC::pthread_join(thread, &result), 0);
  ASSERT_NE(result, PTHREAD_CANCELED);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(
      LIBC::close(static_cast<int>(reinterpret_cast<uintptr_t>(result) - 1)),
      0);
  return 0;
}
