//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

// Exercise the target-owned adapter with real host pthread synchronization.
// REQUIRES: target={{.*linux.*}}
// UNSUPPORTED: libunwind-no-threads
// RUN: %{cxx} %s -pthread -o %t.exe
// RUN: %{exec} %t.exe

#define _LIBUNWIND_HIDDEN
#include "../../src/mmix/RWMutex.hpp"

static libunwind::RWMutex mutex;
static unsigned count;

static void *worker(void *) {
  for (unsigned i = 0; i != 10000; ++i) {
    // Readers must also exclude writers; both modes mutate the test counter.
    bool shared = i % 2;
    shared ? mutex.lock_shared() : mutex.lock();
    ++count;
    shared ? mutex.unlock_shared() : mutex.unlock();
  }
  return nullptr;
}

int main() {
  pthread_t threads[3];
  for (auto &thread : threads)
    if (pthread_create(&thread, nullptr, worker, nullptr))
      abort();
  worker(nullptr);
  for (auto &thread : threads)
    if (pthread_join(thread, nullptr))
      abort();
  if (count != 40000)
    abort();
}
