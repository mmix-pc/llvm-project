//===-- Tests for MMIX AIO suspend registration ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/linux/mmix/aio_suspend_registration.h"
#include "test/UnitTest/Test.h"
#include <errno.h>
#include <signal.h>

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::mmix::AioGate;
using Stack = LIBC_NAMESPACE::mmix::JumpCleanupStack;
using Registry = LIBC_NAMESPACE::AioSuspendObservers<int>;
using Wait = LIBC_NAMESPACE::mmix::AioSuspendRegistration<int>;
static_assert(__is_trivially_destructible(Wait));

namespace {
uint64_t mask() {
  uint64_t mask = 0;
  if (LIBC_NAMESPACE::syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK,
                                         nullptr, &mask, 8))
    __builtin_trap();
  return mask;
}
void set_mask(uint64_t value) {
  if (LIBC_NAMESPACE::syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK,
                                         &value, nullptr, 8))
    __builtin_trap();
}
void complete(AioGate &gate, Registry &registry, const int *request) {
  AioGate::Guard guard(gate);
  if (!guard)
    __builtin_trap();
  registry.complete(request);
}
auto pending = [](const int *) -> ErrorOr<bool> { return false; };
} // namespace

TEST(LlvmLibcAioSuspendRegistrationTest, CompletionAndExplicitFinish) {
  Registry registry;
  AioGate gate;
  Stack::Node *head = nullptr;
  int request = 1;
  const int *list[] = {&request, nullptr, &request};
  Wait wait(registry, gate, head);
  auto saved = mask();
  errno = EDOM;
  ASSERT_EQ(wait.begin(list, 3, pending), 0);
  EXPECT_NE(head, nullptr);
  EXPECT_EQ(mask(), saved);
  EXPECT_FALSE(wait.completion().ready());
  complete(gate, registry, &request);
  EXPECT_TRUE(wait.completion().ready());
  EXPECT_EQ(wait.finish(), 0);
  EXPECT_EQ(head, nullptr);
  EXPECT_EQ(mask(), saved);
  EXPECT_EQ(errno, EDOM);
  complete(gate, registry, &request);
}

TEST(LlvmLibcAioSuspendRegistrationTest, AlreadyCompletedAndSingleUse) {
  Registry registry;
  AioGate gate;
  Stack::Node *head = nullptr;
  int request = 1;
  const int *list[] = {&request};
  Wait wait(registry, gate, head);
  ASSERT_EQ(
      wait.begin(list, 1, [](const int *) -> ErrorOr<bool> { return true; }),
      0);
  EXPECT_TRUE(wait.completion().ready());
  EXPECT_EQ(wait.begin(list, 1, pending), EBUSY);
  EXPECT_EQ(wait.finish(), 0);
  EXPECT_EQ(wait.finish(), EINVAL);
  EXPECT_EQ(wait.begin(list, 1, pending), EBUSY);
}

TEST(LlvmLibcAioSuspendRegistrationTest, InvalidListRollback) {
  Registry registry;
  AioGate gate;
  Stack::Node *head = nullptr;
  Wait wait(registry, gate, head);
  auto saved = mask();
  EXPECT_EQ(wait.begin(nullptr, 1, pending), EINVAL);
  EXPECT_EQ(head, nullptr);
  EXPECT_EQ(mask(), saved);
  EXPECT_EQ(wait.finish(), EINVAL);
}

TEST(LlvmLibcAioSuspendRegistrationTest, InvalidBindingAfterCompletedEntry) {
  Registry registry;
  AioGate gate;
  Stack::Node *head = nullptr;
  int request = 1, invalid = 2;
  const int *list[] = {&request, &invalid};
  Wait wait(registry, gate, head);
  EXPECT_EQ(wait.begin(list, 2,
                       [&](const int *entry) -> ErrorOr<bool> {
                         if (entry == &invalid)
                           return Error(EINVAL);
                         return true;
                       }),
            EINVAL);
  EXPECT_FALSE(wait.completion().ready());
  EXPECT_EQ(head, nullptr);
  complete(gate, registry, &request);
  EXPECT_FALSE(wait.completion().ready());
}

TEST(LlvmLibcAioSuspendRegistrationTest, NestedFinishPreservesOuter) {
  Registry registry;
  AioGate gate;
  Stack::Node *head = nullptr;
  int request = 1;
  const int *list[] = {&request};
  Wait outer(registry, gate, head), inner(registry, gate, head);
  ASSERT_EQ(outer.begin(list, 1, pending), 0);
  auto checkpoint = head;
  ASSERT_EQ(inner.begin(list, 1, pending), 0);
  EXPECT_EQ(outer.finish(), EBUSY);
  EXPECT_EQ(inner.finish(), 0);
  EXPECT_EQ(head, checkpoint);
  complete(gate, registry, &request);
  EXPECT_TRUE(outer.completion().ready());
  EXPECT_FALSE(inner.completion().ready());
  EXPECT_EQ(outer.finish(), 0);
}

TEST(LlvmLibcAioSuspendRegistrationTest, JumpDetachPreservesTarget) {
  Registry registry;
  AioGate gate;
  Stack::Node *head = nullptr;
  int request = 1;
  const int *list[] = {&request};
  Wait outer(registry, gate, head), inner(registry, gate, head);
  ASSERT_EQ(outer.begin(list, 1, pending), 0);
  auto checkpoint = head;
  ASSERT_EQ(inner.begin(list, 1, pending), 0);
  auto saved = mask();
  set_mask(UINT64_MAX);
  EXPECT_TRUE(Stack::unwind(head, checkpoint));
  set_mask(saved);
  complete(gate, registry, &request);
  EXPECT_TRUE(outer.completion().ready());
  EXPECT_FALSE(inner.completion().ready());
  EXPECT_EQ(inner.finish(), EINVAL);
  EXPECT_EQ(outer.finish(), 0);
  EXPECT_EQ(head, nullptr);
}

TEST(LlvmLibcAioSuspendRegistrationTest, EmptyListDoesNotInventCompletion) {
  Registry registry;
  AioGate gate;
  Stack::Node *head = nullptr;
  Wait wait(registry, gate, head);
  ASSERT_EQ(wait.begin(nullptr, 0, pending), 0);
  int request = 1;
  complete(gate, registry, &request);
  EXPECT_FALSE(wait.completion().ready());
  EXPECT_EQ(wait.finish(), 0);
}
