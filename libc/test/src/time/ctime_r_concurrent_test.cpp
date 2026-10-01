//===-- Concurrent ctime_r tests ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/atomic.h"
#include "src/time/ctime_r.h"
#include "test/UnitTest/Test.h"
#include <pthread.h>
#include <sched.h>

namespace {
struct Input {
  time_t timestamp;
  const char *expected;
};
LIBC_NAMESPACE::cpp::Atomic<unsigned> ready{0};
LIBC_NAMESPACE::cpp::Atomic<bool> start{false};

void *format(void *arg) {
  const auto &input = *static_cast<Input *>(arg);
  ready.fetch_add(1);
  while (!start.load())
    sched_yield();
  for (unsigned i = 0; i != 4096; ++i) {
    char buffer[26];
    if (LIBC_NAMESPACE::ctime_r(&input.timestamp, buffer) != buffer)
      return arg;
    for (unsigned j = 0; j != 25; ++j)
      if (buffer[j] != input.expected[j])
        return arg;
  }
  return nullptr;
}
} // namespace

TEST(LlvmLibcCtimeRConcurrentTest, IndependentCalendars) {
  Input inputs[] = {{0, "Thu Jan  1 00:00:00 1970\n"},
                    {2147483647, "Tue Jan 19 03:14:07 2038\n"}};
  pthread_t threads[2];
  for (unsigned i = 0; i != 2; ++i)
    ASSERT_EQ(pthread_create(&threads[i], nullptr, format, &inputs[i]), 0);
  while (ready.load() != 2)
    sched_yield();
  start.store(true);
  for (auto thread : threads) {
    void *result = inputs;
    ASSERT_EQ(pthread_join(thread, &result), 0);
    EXPECT_TRUE(result == nullptr);
  }
}
