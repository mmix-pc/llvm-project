//===-- Native tests for MMIX private jump cleanup ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/aio_suspend_observers.h"
#include "src/setjmp/mmix/linux/jump_cleanup_stack.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::mmix::JumpCleanupStack;
using Node = JumpCleanupStack::Node;
using Observers = LIBC_NAMESPACE::AioSuspendObservers<int>;

static_assert(alignof(Node) >= 8);
static_assert(!__is_constructible(Node, const Node &));
static_assert(!__is_constructible(Node, Node &&));
static_assert(!__is_assignable(Node &, const Node &));
static_assert(!__is_assignable(Node &, Node &&));

namespace {

// Single-threaded logical ordering tests, with no delivered signals or actual
// longjmp. Model the service gate, but attach/detach real AIO observers. Every
// observer operation is gated; cleanup itself starts outside the service gate.
struct Service {
  Observers observers;
  bool locked = false;
  unsigned order[3]{};
  unsigned detached = 0;

  struct Guard {
    Service &service;
    explicit Guard(Service &service) : service(service) {
      if (service.locked)
        __builtin_trap();
      service.locked = true;
    }
    ~Guard() { service.locked = false; }
  };

  void complete(const int *request) {
    Guard guard(*this);
    observers.complete(request);
  }
};

struct Wait {
  Service &service;
  Node *&head;
  unsigned id;
  Node node;
  Observers::Observer observer;
  int request = 0;
  const int *list[1] = {&request};
  unsigned wakes = 0;
  unsigned cleanups = 0;
  bool linked_during_detach = false;

  Wait(Service &service, Node *&head, unsigned id)
      : service(service), head(head), id(id) {}

  static void wake(void *context) {
    auto &wait = *static_cast<Wait *>(context);
    if (!wait.service.locked)
      __builtin_trap();
    ++wait.wakes;
  }

  static bool cleanup(void *context) {
    auto &wait = *static_cast<Wait *>(context);
    Service::Guard guard(wait.service);
    ++wait.cleanups;
    wait.linked_during_detach = wait.head == &wait.node && wait.node.linked();
    if (!wait.service.observers.detach(wait.observer))
      return false;
    if (wait.service.detached == 3)
      __builtin_trap();
    wait.service.order[wait.service.detached++] = wait.id;
    return true;
  }

  bool attach() {
    Service::Guard guard(service);
    auto pending = [](const int *) -> ErrorOr<bool> { return false; };
    return bool(
        service.observers.attach(observer, list, 1, pending, wake, this));
  }

  bool attached() {
    Service::Guard guard(service);
    return observer.attached();
  }

  bool ready() {
    Service::Guard guard(service);
    return observer.ready();
  }

  bool push() { return JumpCleanupStack::push(head, node, cleanup, this); }
};

bool count_cleanup(void *context) {
  ++*static_cast<unsigned *>(context);
  return true;
}

} // namespace

TEST(LlvmLibcMmixJumpCleanupStackTest, EmptyAndNullHook) {
  Node *head = nullptr;
  Node node;
  EXPECT_TRUE(JumpCleanupStack::unwind(head, nullptr));
  EXPECT_FALSE(JumpCleanupStack::unwind(head, &node));
  EXPECT_FALSE(JumpCleanupStack::pop(head, node));
  EXPECT_FALSE(JumpCleanupStack::push(head, node, nullptr, nullptr));
  EXPECT_EQ(head, nullptr);
  EXPECT_FALSE(node.linked());
  auto no_context = [](void *context) { return context == nullptr; };
  ASSERT_TRUE(JumpCleanupStack::push(head, node, no_context, nullptr));
  EXPECT_TRUE(JumpCleanupStack::pop(head, node));
  EXPECT_FALSE(node.linked());
  EXPECT_EQ(head, nullptr);
}

TEST(LlvmLibcMmixJumpCleanupStackTest, CompletionBeforeAndAfterCleanup) {
  const bool completion_orders[] = {false, true};
  for (bool complete_first : completion_orders) {
    Service service;
    Node *head = nullptr;
    Wait wait(service, head, 1);
    ASSERT_TRUE(wait.attach());
    ASSERT_TRUE(wait.push());
    if (complete_first) {
      service.complete(&wait.request);
      EXPECT_TRUE(wait.ready());
      EXPECT_EQ(wait.wakes, 1U);
    }
    EXPECT_TRUE(JumpCleanupStack::pop(head, wait.node));
    EXPECT_TRUE(wait.linked_during_detach);
    EXPECT_FALSE(wait.node.linked());
    EXPECT_FALSE(wait.attached());
    EXPECT_EQ(head, nullptr);
    service.complete(&wait.request);
    service.complete(&wait.request);
    EXPECT_EQ(wait.wakes, complete_first ? 1U : 0U);
    EXPECT_EQ(wait.cleanups, 1U);
    EXPECT_FALSE(JumpCleanupStack::pop(head, wait.node));
    EXPECT_TRUE(JumpCleanupStack::unwind(head, nullptr));
    EXPECT_EQ(wait.cleanups, 1U);
  }
}

TEST(LlvmLibcMmixJumpCleanupStackTest, SameAndInnerCheckpointPreserveOuter) {
  Service service;
  Node *head = nullptr;
  Wait outer(service, head, 1), inner(service, head, 2),
      newer(service, head, 3);
  ASSERT_TRUE(outer.attach());
  ASSERT_TRUE(outer.push());
  Node *outer_checkpoint = head;
  EXPECT_TRUE(JumpCleanupStack::unwind(head, outer_checkpoint));
  EXPECT_EQ(outer.cleanups, 0U);
  ASSERT_TRUE(inner.attach());
  ASSERT_TRUE(inner.push());
  Node *inner_checkpoint = head;
  EXPECT_TRUE(JumpCleanupStack::unwind(head, inner_checkpoint));
  EXPECT_EQ(head, &inner.node);
  EXPECT_EQ(inner.cleanups, 0U);
  EXPECT_TRUE(outer.attached());
  EXPECT_TRUE(inner.attached());
  ASSERT_TRUE(newer.attach());
  ASSERT_TRUE(newer.push());
  EXPECT_TRUE(JumpCleanupStack::unwind(head, inner_checkpoint));
  EXPECT_EQ(head, &inner.node);
  EXPECT_EQ(newer.cleanups, 1U);
  EXPECT_TRUE(newer.linked_during_detach);
  EXPECT_FALSE(newer.attached());
  EXPECT_TRUE(inner.attached());
  EXPECT_TRUE(outer.attached());
  EXPECT_EQ(inner.cleanups, 0U);
  EXPECT_EQ(outer.cleanups, 0U);
  service.complete(&newer.request);
  EXPECT_EQ(newer.wakes, 0U);
  EXPECT_TRUE(JumpCleanupStack::unwind(head, outer_checkpoint));
  EXPECT_EQ(head, &outer.node);
  EXPECT_EQ(inner.cleanups, 1U);
  EXPECT_TRUE(inner.linked_during_detach);
  EXPECT_FALSE(inner.attached());
  EXPECT_EQ(outer.cleanups, 0U);
  service.complete(&inner.request);
  service.complete(&outer.request);
  EXPECT_EQ(inner.wakes, 0U);
  EXPECT_EQ(outer.wakes, 1U);
  EXPECT_TRUE(outer.ready());
  EXPECT_TRUE(JumpCleanupStack::unwind(head, nullptr));
  EXPECT_EQ(outer.cleanups, 1U);
}

TEST(LlvmLibcMmixJumpCleanupStackTest, CheckpointInNoWaitIntermediateSignal) {
  Service service;
  Node *head = nullptr;
  Wait outer(service, head, 1), nested(service, head, 2);
  ASSERT_TRUE(outer.attach());
  ASSERT_TRUE(outer.push());
  // Model an intermediate signal frame with no wait of its own. Its saved
  // cleanup head is still the outer wait, not a frame-specific sentinel.
  Node *intermediate_checkpoint = head;
  ASSERT_TRUE(nested.attach());
  ASSERT_TRUE(nested.push());
  EXPECT_TRUE(JumpCleanupStack::unwind(head, intermediate_checkpoint));
  EXPECT_EQ(head, &outer.node);
  EXPECT_TRUE(outer.attached());
  EXPECT_FALSE(nested.attached());
  EXPECT_EQ(outer.cleanups, 0U);
  EXPECT_EQ(nested.cleanups, 1U);
  EXPECT_TRUE(JumpCleanupStack::unwind(head, intermediate_checkpoint));
  EXPECT_EQ(nested.cleanups, 1U);
  service.complete(&nested.request);
  service.complete(&outer.request);
  EXPECT_EQ(nested.wakes, 0U);
  EXPECT_EQ(outer.wakes, 1U);
  EXPECT_TRUE(JumpCleanupStack::pop(head, outer.node));
}

TEST(LlvmLibcMmixJumpCleanupStackTest, NestedThreeUnwindInLifoOrder) {
  Service service;
  Node *head = nullptr;
  Wait outer(service, head, 1), middle(service, head, 2),
      inner(service, head, 3);
  ASSERT_TRUE(outer.attach());
  ASSERT_TRUE(outer.push());
  ASSERT_TRUE(middle.attach());
  ASSERT_TRUE(middle.push());
  ASSERT_TRUE(inner.attach());
  ASSERT_TRUE(inner.push());
  service.complete(&middle.request);
  EXPECT_TRUE(JumpCleanupStack::unwind(head, &outer.node));
  EXPECT_EQ(head, &outer.node);
  EXPECT_EQ(service.detached, 2U);
  EXPECT_EQ(service.order[0], 3U);
  EXPECT_EQ(service.order[1], 2U);
  EXPECT_TRUE(inner.linked_during_detach);
  EXPECT_TRUE(middle.linked_during_detach);
  EXPECT_TRUE(outer.attached());
  EXPECT_FALSE(middle.attached());
  EXPECT_FALSE(inner.attached());
  service.complete(&inner.request);
  service.complete(&middle.request);
  EXPECT_EQ(inner.wakes, 0U);
  EXPECT_EQ(middle.wakes, 1U);
  EXPECT_TRUE(JumpCleanupStack::unwind(head, nullptr));
  EXPECT_EQ(head, nullptr);
  EXPECT_EQ(service.detached, 3U);
  EXPECT_EQ(service.order[2], 1U);
  EXPECT_EQ(outer.cleanups, 1U);
  EXPECT_EQ(middle.cleanups, 1U);
  EXPECT_EQ(inner.cleanups, 1U);
  service.complete(&outer.request);
  EXPECT_EQ(outer.wakes, 0U);
}

TEST(LlvmLibcMmixJumpCleanupStackTest, InvalidTargetRollsBackWithoutHooks) {
  Service service;
  Node *head = nullptr;
  Wait outer(service, head, 1), inner(service, head, 2);
  ASSERT_TRUE(outer.attach());
  ASSERT_TRUE(outer.push());
  ASSERT_TRUE(inner.attach());
  ASSERT_TRUE(inner.push());
  Node absent;
  EXPECT_FALSE(JumpCleanupStack::unwind(head, &absent));
  // An aligned but inaccessible address must only be compared, not read.
  EXPECT_FALSE(
      JumpCleanupStack::unwind(head, reinterpret_cast<Node *>(alignof(Node))));
  EXPECT_EQ(head, &inner.node);
  EXPECT_TRUE(inner.node.linked());
  EXPECT_TRUE(outer.node.linked());
  EXPECT_EQ(inner.cleanups, 0U);
  EXPECT_EQ(outer.cleanups, 0U);
  EXPECT_EQ(service.detached, 0U);
  EXPECT_TRUE(inner.attached());
  EXPECT_TRUE(outer.attached());
  service.complete(&inner.request);
  service.complete(&outer.request);
  EXPECT_EQ(inner.wakes, 1U);
  EXPECT_EQ(outer.wakes, 1U);
  EXPECT_TRUE(JumpCleanupStack::unwind(head, nullptr));
  EXPECT_EQ(service.order[0], 2U);
  EXPECT_EQ(service.order[1], 1U);
}

TEST(LlvmLibcMmixJumpCleanupStackTest, WrongOwnerAndNonLifoPop) {
  Node *head = nullptr;
  Node *other = nullptr;
  Node outer, inner, foreign, unused;
  unsigned calls = 0;
  ASSERT_TRUE(JumpCleanupStack::push(head, outer, count_cleanup, &calls));
  ASSERT_TRUE(JumpCleanupStack::push(head, inner, count_cleanup, &calls));
  ASSERT_TRUE(JumpCleanupStack::push(other, foreign, count_cleanup, &calls));
  EXPECT_FALSE(JumpCleanupStack::pop(head, outer));
  EXPECT_FALSE(JumpCleanupStack::pop(other, inner));
  EXPECT_FALSE(JumpCleanupStack::pop(head, foreign));
  EXPECT_FALSE(JumpCleanupStack::push(head, inner, count_cleanup, &calls));
  EXPECT_FALSE(JumpCleanupStack::push(other, inner, count_cleanup, &calls));
  EXPECT_FALSE(JumpCleanupStack::unwind(head, &foreign));
  EXPECT_FALSE(JumpCleanupStack::unwind(other, &inner));
  // A copied head value does not confer ownership of the original stack.
  Node *alias = head;
  EXPECT_FALSE(JumpCleanupStack::pop(alias, inner));
  EXPECT_FALSE(JumpCleanupStack::push(alias, unused, count_cleanup, &calls));
  EXPECT_FALSE(JumpCleanupStack::unwind(alias, alias));
  EXPECT_FALSE(JumpCleanupStack::unwind(alias, nullptr));
  EXPECT_EQ(alias, &inner);
  EXPECT_FALSE(unused.linked());
  EXPECT_EQ(head, &inner);
  EXPECT_EQ(other, &foreign);
  EXPECT_EQ(calls, 0U);
  EXPECT_TRUE(JumpCleanupStack::pop(head, inner));
  EXPECT_EQ(head, &outer);
  EXPECT_TRUE(JumpCleanupStack::unwind(head, nullptr));
  EXPECT_TRUE(JumpCleanupStack::pop(other, foreign));
  EXPECT_EQ(calls, 3U);
}

TEST(LlvmLibcMmixJumpCleanupStackTest, NodeReuseUpdatesOwnerHookAndContext) {
  Node *head = nullptr;
  Node *other = nullptr;
  Node node;
  unsigned first = 0, second = 0;
  ASSERT_TRUE(JumpCleanupStack::push(head, node, count_cleanup, &first));
  EXPECT_TRUE(JumpCleanupStack::pop(head, node));
  EXPECT_FALSE(node.linked());
  auto add_two = [](void *context) {
    *static_cast<unsigned *>(context) += 2;
    return true;
  };
  ASSERT_TRUE(JumpCleanupStack::push(other, node, add_two, &second));
  EXPECT_FALSE(JumpCleanupStack::unwind(head, &node));
  EXPECT_TRUE(JumpCleanupStack::unwind(other, nullptr));
  EXPECT_FALSE(node.linked());
  ASSERT_TRUE(JumpCleanupStack::push(head, node, count_cleanup, &first));
  EXPECT_TRUE(JumpCleanupStack::unwind(head, nullptr));
  EXPECT_EQ(head, nullptr);
  EXPECT_EQ(other, nullptr);
  EXPECT_EQ(first, 2U);
  EXPECT_EQ(second, 2U);
}

TEST(LlvmLibcMmixJumpCleanupStackTest, ObserverAndNodeReuseAfterUnwind) {
  Service service;
  Node *head = nullptr;
  Wait wait(service, head, 1);
  ASSERT_TRUE(wait.attach());
  ASSERT_TRUE(wait.push());
  EXPECT_TRUE(JumpCleanupStack::unwind(head, nullptr));
  service.complete(&wait.request);
  EXPECT_EQ(wait.wakes, 0U);
  ASSERT_TRUE(wait.attach());
  ASSERT_TRUE(wait.push());
  EXPECT_FALSE(wait.ready());
  service.complete(&wait.request);
  EXPECT_TRUE(wait.ready());
  EXPECT_EQ(wait.wakes, 1U);
  EXPECT_TRUE(JumpCleanupStack::pop(head, wait.node));
  EXPECT_TRUE(wait.linked_during_detach);
  EXPECT_EQ(wait.cleanups, 2U);
  EXPECT_FALSE(wait.attached());
  EXPECT_FALSE(wait.node.linked());
  EXPECT_EQ(head, nullptr);
}

TEST(LlvmLibcMmixJumpCleanupStackTest, LinkedNodeDestructionIsFatal) {
  EXPECT_DEATH(
      [] {
        Node *head = nullptr;
        unsigned calls = 0;
        Node node;
        JumpCleanupStack::push(head, node, count_cleanup, &calls);
      },
      WITH_SIGNAL(-1));
}

TEST(LlvmLibcMmixJumpCleanupStackTest, FailedPopHookIsFatal) {
  EXPECT_DEATH(
      [] {
        Node *head = nullptr;
        Node node;
        bool allowed = false;
        auto allowed_cleanup = [](void *p) { return *static_cast<bool *>(p); };
        JumpCleanupStack::push(head, node, allowed_cleanup, &allowed);
        JumpCleanupStack::pop(head, node);
        // If failure incorrectly returns, avoid a destructor trap masking it.
        allowed = true;
        JumpCleanupStack::pop(head, node);
      },
      WITH_SIGNAL(-1));
}

TEST(LlvmLibcMmixJumpCleanupStackTest, FailedUnwindHookIsFatal) {
  EXPECT_DEATH(
      [] {
        Node *head = nullptr;
        Node node;
        bool allowed = false;
        auto allowed_cleanup = [](void *p) { return *static_cast<bool *>(p); };
        JumpCleanupStack::push(head, node, allowed_cleanup, &allowed);
        JumpCleanupStack::unwind(head, nullptr);
        allowed = true;
        JumpCleanupStack::pop(head, node);
      },
      WITH_SIGNAL(-1));
}
