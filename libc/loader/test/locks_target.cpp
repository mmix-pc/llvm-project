//===-- MMIX loader lock and cancellation probe ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/locks.h"
#include "src/__support/threads/linux/mmix/loader_cancellation.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

using namespace LIBC_NAMESPACE;
using namespace LIBC_NAMESPACE::loader;
using Kind = LoaderLocks::Kind;
static LoaderLocks locks;
static unsigned count, cleanups, reached, deferred_return;

static void check(bool ok, int line = __builtin_LINE()) {
  if (!ok) {
    printf("LOADER_LOCKS FAIL %d\n", line);
    _Exit(1);
  }
}
static void *worker(void *) {
  for (unsigned i = 0; i < 16; ++i) {
    LoaderLocks::Session session(locks, mmix::loader_cancellation_hooks());
    check(!session.error());
    LoaderLocks::Guard mutation(session, Kind::Mutation);
    check(!mutation.error());
    {
      LoaderLocks::Session nested(locks, mmix::loader_cancellation_hooks());
      LoaderLocks::Guard recursive(nested, Kind::Mutation);
      check(!recursive.error());
    }
    LoaderLocks::Guard registry(session, Kind::Registry);
    check(!registry.error());
    LoaderLocks::Guard arena(session, Kind::Arena);
    check(!arena.error());
    ++count;
  }
  return nullptr;
}
static void cleanup(void *) {
  // Reacquiring every lock witnesses that cancellation ran after all releases.
  LoaderLocks::Session session(locks, mmix::loader_cancellation_hooks());
  check(!session.error());
  LoaderLocks::Guard mutation(session, Kind::Mutation);
  LoaderLocks::Guard registry(session, Kind::Registry);
  LoaderLocks::Guard arena(session, Kind::Arena);
  check(!mutation.error() && !registry.error() && !arena.error());
  ++cleanups;
}
static void *cancelled(void *mode) {
  pthread_cleanup_push(cleanup, nullptr);
  check(!pthread_setcanceltype(
      mode ? PTHREAD_CANCEL_ASYNCHRONOUS : PTHREAD_CANCEL_DEFERRED, nullptr));
  {
    LoaderLocks::Session session(locks, mmix::loader_cancellation_hooks());
    LoaderLocks::Guard mutation(session, Kind::Mutation);
    LoaderLocks::Guard registry(session, Kind::Registry);
    check(!session.error() && !mutation.error() && !registry.error());
    check(!pthread_cancel(pthread_self()));
    pthread_testcancel();
    ++reached;
  }
  check(!mode);      // Pending asynchronous cancellation must run on restore.
  ++deferred_return; // Loader scope is not a deferred cancellation point.
  pthread_testcancel();
  check(false);
  pthread_cleanup_pop(0);
  return nullptr;
}
extern "C" int main() {
  pthread_t threads[4];
  for (auto &thread : threads)
    check(!pthread_create(&thread, nullptr, worker, nullptr));
  for (auto &thread : threads)
    check(!pthread_join(thread, nullptr));
  check(count == 64);
  for (unsigned mode = 0; mode != 2; ++mode) {
    pthread_t thread;
    void *result = nullptr;
    check(!pthread_create(
        &thread, nullptr, cancelled,
        reinterpret_cast<void *>(static_cast<uintptr_t>(mode))));
    check(!pthread_join(thread, &result) && result == PTHREAD_CANCELED);
  }
  check(cleanups == 2 && reached == 2 && deferred_return == 1);
  puts("LOADER_LOCKS PASS workers=4 operations=64 cancellations=2");
  return 0;
}
