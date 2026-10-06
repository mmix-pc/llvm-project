//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03, no-threads, no-exceptions
// REQUIRES: target={{.+-linux-.*}}, libcpp-has-thread-api-pthread

#include <atomic>
#include <cassert>
#include <cstdio>
#include <mutex>
#include <pthread.h>
#include <sched.h>

static std::once_flag flag;
static std::atomic<bool> initialized(false), release(false), waiting(false);
static unsigned calls;

static void initialize() {
  ++calls;
  initialized.store(true, std::memory_order_release);
  while (!release.load(std::memory_order_acquire))
    sched_yield();
}
static void* owner(void*) {
  std::call_once(flag, initialize);
  return nullptr;
}
static void* contender(void*) {
  waiting.store(true, std::memory_order_release);
  std::call_once(flag, initialize);
  assert(false);
  return nullptr;
}

int main(int, char**) {
  pthread_t first, second;
  assert(pthread_create(&first, nullptr, owner, nullptr) == 0);
  while (!initialized.load(std::memory_order_acquire))
    sched_yield();
  assert(pthread_create(&second, nullptr, contender, nullptr) == 0);
  while (!waiting.load(std::memory_order_acquire))
    sched_yield();
  // The initializer cannot finish yet. A request before the wait is pending
  // until its internal condition wait; no scheduling delay supplies admission.
  assert(pthread_cancel(second) == 0);
  void* result = nullptr;
  assert(pthread_join(second, &result) == 0);
  assert(result == PTHREAD_CANCELED);
  std::fputs("CALL_ONCE_CANCELED_WAITER\n", stderr);
  release.store(true, std::memory_order_release);
  assert(pthread_join(first, nullptr) == 0);
  std::call_once(flag, [] { assert(false); });
  assert(calls == 1);
  std::once_flag other;
  bool completed = false;
  std::call_once(other, [&] { completed = true; });
  assert(completed);
  return 0;
}
