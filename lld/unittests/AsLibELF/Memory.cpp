//===- Memory.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Memory.h"
#include "gtest/gtest.h"

#if LLVM_ENABLE_THREADS
#include <atomic>
#include <thread>
#endif

using namespace lld;

namespace {
struct Tracked {
  int *live = nullptr;
  explicit Tracked(int *live = nullptr) : live(live) {
    if (live)
      ++*live;
  }
  ~Tracked() {
    if (live)
      --*live;
  }
};

#if LLVM_ENABLE_THREADS
TEST(Memory, ThreadLocalLifetime) {
  std::atomic<unsigned> ready{0};
  const void *allocators[2] = {};
  int live[2] = {};
  auto worker = [&](unsigned index) {
    auto &allocator = getSpecificAllocSingletonThreadLocal<Tracked>();
    allocators[index] = &allocator;
    EXPECT_EQ(makeThreadLocal<Tracked>(&live[index])->live, &live[index]);
    auto *array = makeThreadLocalN<Tracked>(2);
    for (unsigned i = 0; i != 2; ++i) {
      array[i].live = &live[index];
      ++live[index];
    }
    EXPECT_EQ(live[index], 3);
    ++ready;
    while (ready.load() != 2)
      std::this_thread::yield();
  };
  std::thread first(worker, 0), second(worker, 1);
  first.join();
  second.join();
  EXPECT_NE(allocators[0], allocators[1]);
  EXPECT_EQ(live[0], 0);
  EXPECT_EQ(live[1], 0);
}
#else
TEST(Memory, NoThreadContextLifetime) {
  int live = 0;
  for (unsigned iteration = 0; iteration != 2; ++iteration) {
    {
      CommonLinkerContext context;
      ASSERT_EQ(&getSpecificAllocSingletonThreadLocal<Tracked>(),
                &getSpecificAllocSingleton<Tracked>());
      EXPECT_EQ(makeThreadLocal<Tracked>(&live)->live, &live);
      auto *array = makeThreadLocalN<Tracked>(2);
      for (unsigned i = 0; i != 2; ++i) {
        array[i].live = &live;
        ++live;
      }
      EXPECT_EQ(live, 3);
    }
    EXPECT_EQ(live, 0);
    EXPECT_FALSE(hasContext());
  }
}
#endif
} // namespace
