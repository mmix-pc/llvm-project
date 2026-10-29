//===-- Tests for concurrent environment initialization -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "src/__support/CPP/atomic.h"
#include "src/stdlib/getenv.h"
#include "test/UnitTest/Test.h"
#ifdef LIBC_COPT_SUPPORT_ENVIRON
#include "src/unistd/environ.h"
#endif
#include <pthread.h>
#include <sched.h>

namespace LIBC_NAMESPACE_DECL {
AppProperties app;
#ifdef LIBC_TEST_ENVIRON_STUB
char **environ;
#endif
} // namespace LIBC_NAMESPACE_DECL

namespace {
using LIBC_NAMESPACE::cpp::Atomic;
constexpr unsigned THREADS = 8;
constexpr unsigned ENTRIES = 16384;
char first[] = "FIRST=begin";
char last[] = "LAST=end";
char *environment[ENTRIES + 1];
Atomic<unsigned> ready{0};
Atomic<bool> start{false};

void *lookup(void *) {
  ready.fetch_add(1);
  while (!start.load())
    sched_yield();
  for (unsigned i = 0; i != 16; ++i) {
    if (LIBC_NAMESPACE::getenv("FIRST") != first + 6 ||
        LIBC_NAMESPACE::getenv("LAST") != last + 5 ||
        LIBC_NAMESPACE::getenv("ABSENT") != nullptr)
      return first;
  }
  return nullptr;
}
} // namespace

// This executable must not call the tested getenv before releasing the peers.
// Race-detector builds also check first-use publication, not just final values.
TEST(LlvmLibcGetenvInitializationTest, ConcurrentFirstLookup) {
  for (unsigned i = 0; i != ENTRIES; ++i)
    environment[i] = first;
  environment[ENTRIES - 1] = last;
  LIBC_NAMESPACE::app.env_ptr = reinterpret_cast<uintptr_t *>(environment);
#ifdef LIBC_COPT_SUPPORT_ENVIRON
  // Before the first getenv, replace only public environ. A stale app.env_ptr
  // must not hide LAST, even if the manager has never been initialized.
  char *startup[] = {first, nullptr};
  LIBC_NAMESPACE::app.env_ptr = reinterpret_cast<uintptr_t *>(startup);
  LIBC_NAMESPACE::environ = environment;
#endif
  pthread_t threads[THREADS];
  for (auto &thread : threads)
    ASSERT_EQ(pthread_create(&thread, nullptr, lookup, nullptr), 0);
  while (ready.load() != THREADS)
    sched_yield();
  start.store(true);
  for (auto thread : threads) {
    void *result = first;
    ASSERT_EQ(pthread_join(thread, &result), 0);
    EXPECT_TRUE(result == nullptr);
  }
  LIBC_NAMESPACE::app.env_ptr = nullptr;
#ifdef LIBC_COPT_SUPPORT_ENVIRON
  LIBC_NAMESPACE::environ = nullptr;
#endif
}
