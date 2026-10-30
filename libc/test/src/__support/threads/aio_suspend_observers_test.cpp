//===-- Tests for allocation-free AIO completion observers ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/aio_suspend_observers.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;
using Observers = LIBC_NAMESPACE::AioSuspendObservers<int>;

namespace {
ErrorOr<bool> pending(const int *) { return false; }
void wake(void *p) { ++*static_cast<unsigned *>(p); }
} // namespace

TEST(LlvmLibcAioSuspendObserversTest, NullDuplicatesAndUnrelatedCompletion) {
  Observers observers;
  Observers::Observer observer;
  int a = 0, b = 0;
  const int *list[] = {nullptr, &a, &a};
  unsigned wakes = 0;
  ASSERT_TRUE(bool(observers.attach(observer, list, 3, pending, wake, &wakes)));
  observers.complete(nullptr);
  observers.complete(&b);
  EXPECT_FALSE(observer.ready());
  EXPECT_EQ(wakes, 0U);
  observers.complete(&a);
  EXPECT_TRUE(observer.ready());
  EXPECT_EQ(wakes, 1U);
  observers.complete(&a);
  EXPECT_EQ(wakes, 1U);
  EXPECT_TRUE(observers.detach(observer));
  EXPECT_FALSE(observer.ready());
  EXPECT_FALSE(observers.detach(observer));
}

TEST(LlvmLibcAioSuspendObserversTest, ExistingCompletionDoesNotNeedWake) {
  Observers observers;
  Observers::Observer observer;
  int a = 0;
  const int *list[] = {&a};
  unsigned wakes = 0;
  auto done = [](const int *) -> ErrorOr<bool> { return true; };
  ASSERT_TRUE(bool(observers.attach(observer, list, 1, done, wake, &wakes)));
  EXPECT_TRUE(observer.ready());
  observers.complete(&a);
  EXPECT_EQ(wakes, 0U);
  EXPECT_TRUE(observers.detach(observer));
}

TEST(LlvmLibcAioSuspendObserversTest, InvalidListRollsBackEvenAfterCompletion) {
  Observers observers;
  Observers::Observer observer;
  int a = 0, b = 0;
  const int *list[] = {&a, &b};
  unsigned wakes = 0;
  auto query = [&](const int *p) -> ErrorOr<bool> {
    return p == &a ? ErrorOr<bool>(true) : ErrorOr<bool>(Error(EINVAL));
  };
  EXPECT_EQ(observers.attach(observer, list, 2, query, wake, &wakes).error(),
            EINVAL);
  EXPECT_FALSE(observer.attached());
  observers.complete(&a);
  EXPECT_EQ(wakes, 0U);
  ASSERT_TRUE(bool(observers.attach(observer, list, 2, pending, wake, &wakes)));
  EXPECT_EQ(observers.attach(observer, list, 2, pending, wake, &wakes).error(),
            EBUSY);
  EXPECT_TRUE(observers.detach(observer));
}

TEST(LlvmLibcAioSuspendObserversTest, EmptyListsDoNotInventCompletion) {
  Observers observers;
  Observers::Observer empty, nulls;
  const int *list[] = {nullptr, nullptr};
  unsigned wakes = 0;
  ASSERT_TRUE(bool(observers.attach(empty, nullptr, 0, pending, wake, &wakes)));
  ASSERT_TRUE(bool(observers.attach(nulls, list, 2, pending, wake, &wakes)));
  int a = 0;
  observers.complete(&a);
  EXPECT_FALSE(empty.ready());
  EXPECT_FALSE(nulls.ready());
  EXPECT_EQ(wakes, 0U);
  EXPECT_TRUE(observers.detach(empty));
  EXPECT_TRUE(observers.detach(nulls));
  EXPECT_EQ(observers.attach(empty, nullptr, 1, pending, wake, &wakes).error(),
            EINVAL);
  EXPECT_EQ(observers.attach(empty, list, 2, pending, nullptr, nullptr).error(),
            EINVAL);
}

TEST(LlvmLibcAioSuspendObserversTest, NestedAndIndependentWaitsDetachSafely) {
  Observers observers, other;
  Observers::Observer outer, inner, detached;
  int a = 0, b = 0;
  const int *both[] = {&a, &b};
  const int *one[] = {&b};
  unsigned outer_wakes = 0, inner_wakes = 0, detached_wakes = 0;
  ASSERT_TRUE(
      bool(observers.attach(outer, both, 2, pending, wake, &outer_wakes)));
  ASSERT_TRUE(
      bool(observers.attach(detached, one, 1, pending, wake, &detached_wakes)));
  ASSERT_TRUE(
      bool(observers.attach(inner, one, 1, pending, wake, &inner_wakes)));
  EXPECT_FALSE(other.detach(inner));
  EXPECT_TRUE(observers.detach(detached));
  observers.complete(&b);
  EXPECT_EQ(outer_wakes, 1U);
  EXPECT_EQ(inner_wakes, 1U);
  EXPECT_EQ(detached_wakes, 0U);
  EXPECT_TRUE(observers.detach(outer));
  EXPECT_TRUE(observers.detach(inner));
}

TEST(LlvmLibcAioSuspendObserversTest, ListLengthIsNotAStorageLimit) {
  Observers observers;
  Observers::Observer observer;
  int a = 0;
  const int *list[1024];
  for (auto &item : list)
    item = &a;
  unsigned wakes = 0;
  ASSERT_TRUE(
      bool(observers.attach(observer, list, 1024, pending, wake, &wakes)));
  observers.complete(&a);
  EXPECT_TRUE(observer.ready());
  EXPECT_EQ(wakes, 1U);
  EXPECT_TRUE(observers.detach(observer));
}
