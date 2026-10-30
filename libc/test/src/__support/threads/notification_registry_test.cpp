//===-- Tests for notification cookie ownership ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/notification_registry.h"
#include "test/UnitTest/Test.h"

using Registry = LIBC_NAMESPACE::NotificationRegistry;
using Node = Registry::Node;
using Reference = Registry::EventReference;

TEST(LlvmLibcNotificationRegistryTest, RegistrationAndEarlyEvent) {
  Registry registry;
  Node node;
  Reference event;
  EXPECT_TRUE(node.reclaimable());
  EXPECT_FALSE(registry.publish(node));
  EXPECT_FALSE(registry.retire(node));
  EXPECT_FALSE(registry.acquire(0, event));
  ASSERT_TRUE(registry.install(node));
  EXPECT_EQ(node.cookie(), uint64_t(1));
  EXPECT_TRUE(node.installing());
  EXPECT_FALSE(node.live());
  EXPECT_FALSE(node.reclaimable());
  ASSERT_TRUE(registry.acquire(node.cookie(), event));
  EXPECT_EQ(event.get(), &node);
  ASSERT_TRUE(registry.publish(node));
  EXPECT_TRUE(event.get()->live());
  EXPECT_FALSE(registry.publish(node));
  EXPECT_FALSE(registry.install(node));
  EXPECT_EQ(registry.last_issued_cookie(), uint64_t(1));
  ASSERT_TRUE(registry.release(event));
  EXPECT_FALSE(node.reclaimable());
  ASSERT_TRUE(registry.retire(node));
  EXPECT_TRUE(node.reclaimable());
  EXPECT_FALSE(node.live());
}

TEST(LlvmLibcNotificationRegistryTest, DelayedDrainAndDoubleRelease) {
  Registry registry;
  Node node;
  Reference first, second, late;
  ASSERT_TRUE(registry.install(node));
  ASSERT_TRUE(registry.publish(node));
  ASSERT_TRUE(registry.acquire(node.cookie(), first));
  ASSERT_TRUE(registry.acquire(node.cookie(), second));
  EXPECT_FALSE(registry.acquire(node.cookie(), first));
  ASSERT_TRUE(registry.retire(node));
  EXPECT_FALSE(registry.acquire(node.cookie(), late));
  EXPECT_FALSE(registry.retire(node));
  EXPECT_FALSE(registry.publish(node));
  EXPECT_FALSE(registry.install(node));
  EXPECT_FALSE(node.reclaimable());
  ASSERT_TRUE(registry.release(first));
  EXPECT_EQ(first.get(), static_cast<Node *>(nullptr));
  EXPECT_FALSE(registry.release(first));
  EXPECT_FALSE(node.reclaimable());
  EXPECT_EQ(second.get(), &node);
  ASSERT_TRUE(registry.release(second));
  EXPECT_TRUE(node.reclaimable());
  EXPECT_FALSE(registry.release(second));
}

TEST(LlvmLibcNotificationRegistryTest, ReusedNodeRejectsStaleCookie) {
  Registry registry;
  Node node;
  Reference old_event, new_event;
  ASSERT_TRUE(registry.install(node));
  const uint64_t old_cookie = node.cookie();
  ASSERT_TRUE(registry.acquire(old_cookie, old_event));
  ASSERT_TRUE(registry.retire(node));
  ASSERT_TRUE(registry.release(old_event));
  ASSERT_TRUE(registry.install(node));
  ASSERT_TRUE(registry.publish(node));
  EXPECT_GT(node.cookie(), old_cookie);
  EXPECT_FALSE(registry.acquire(old_cookie, old_event));
  ASSERT_TRUE(registry.acquire(node.cookie(), new_event));
  EXPECT_FALSE(registry.release(old_event));
  ASSERT_TRUE(registry.retire(node));
  EXPECT_FALSE(node.reclaimable());
  ASSERT_TRUE(registry.release(new_event));
  EXPECT_TRUE(node.reclaimable());
}

TEST(LlvmLibcNotificationRegistryTest, AcceptedEventCanRetainAfterRetirement) {
  Registry registry, foreign;
  Node node;
  Reference event, callback, empty;
  EXPECT_FALSE(registry.retain(empty, callback));
  ASSERT_TRUE(registry.install(node));
  ASSERT_TRUE(registry.publish(node));
  ASSERT_TRUE(registry.acquire(node.cookie(), event));
  ASSERT_TRUE(registry.retire(node));
  EXPECT_FALSE(foreign.retain(event, callback));
  EXPECT_EQ(callback.get(), static_cast<Node *>(nullptr));
  EXPECT_FALSE(registry.retain(event, event));
  ASSERT_TRUE(registry.retain(event, callback));
  EXPECT_FALSE(registry.retain(event, callback));
  ASSERT_TRUE(registry.release(event));
  EXPECT_FALSE(registry.retain(event, empty));
  EXPECT_FALSE(node.reclaimable());
  EXPECT_FALSE(registry.acquire(node.cookie(), empty));
  ASSERT_TRUE(registry.release(callback));
  EXPECT_TRUE(node.reclaimable());
}

TEST(LlvmLibcNotificationRegistryTest, IndependentRegistries) {
  Registry first, second;
  Node a, b;
  Reference event_a, event_b;
  ASSERT_TRUE(first.install(a));
  ASSERT_TRUE(second.install(b));
  EXPECT_EQ(a.cookie(), b.cookie());
  EXPECT_FALSE(second.install(a));
  EXPECT_FALSE(second.publish(a));
  EXPECT_FALSE(second.retire(a));
  ASSERT_TRUE(first.acquire(a.cookie(), event_a));
  ASSERT_TRUE(second.acquire(b.cookie(), event_b));
  EXPECT_EQ(event_a.get(), &a);
  EXPECT_EQ(event_b.get(), &b);
  EXPECT_FALSE(second.acquire(b.cookie(), event_a));
  EXPECT_FALSE(second.release(event_a));
  EXPECT_EQ(event_a.get(), &a);
  ASSERT_TRUE(first.retire(a));
  EXPECT_FALSE(second.release(event_a));
  EXPECT_FALSE(a.reclaimable());
  ASSERT_TRUE(first.release(event_a));
  ASSERT_TRUE(second.retire(b));
  ASSERT_TRUE(second.release(event_b));
  EXPECT_TRUE(a.reclaimable());
  EXPECT_TRUE(b.reclaimable());
  // Moving drained caller storage to another registry does not carry ownership.
  ASSERT_TRUE(second.install(a));
  EXPECT_EQ(a.cookie(), uint64_t(2));
  EXPECT_FALSE(first.retire(a));
  ASSERT_TRUE(second.retire(a));
}

TEST(LlvmLibcNotificationRegistryTest, ExhaustionDoesNotWrapOrChangeNode) {
  Registry registry(UINT64_MAX - 1);
  Node last, rejected;
  Reference event;
  ASSERT_TRUE(registry.install(last));
  EXPECT_EQ(last.cookie(), UINT64_MAX);
  ASSERT_TRUE(registry.publish(last));
  ASSERT_TRUE(registry.acquire(last.cookie(), event));
  EXPECT_FALSE(registry.install(rejected));
  EXPECT_TRUE(rejected.reclaimable());
  EXPECT_EQ(rejected.cookie(), uint64_t(0));
  ASSERT_TRUE(registry.retire(last));
  ASSERT_TRUE(registry.release(event));
  EXPECT_FALSE(registry.install(last));
  EXPECT_TRUE(last.reclaimable());
  EXPECT_EQ(last.cookie(), UINT64_MAX);
  EXPECT_EQ(registry.last_issued_cookie(), UINT64_MAX);
  EXPECT_FALSE(registry.acquire(0, event));
  EXPECT_FALSE(registry.acquire(UINT64_MAX, event));
}

TEST(LlvmLibcNotificationRegistryTest, DrainedRegistryWatermarkIsPreserved) {
  uint64_t watermark;
  Node node;
  {
    Registry registry;
    ASSERT_TRUE(registry.install(node));
    ASSERT_TRUE(registry.retire(node));
    watermark = registry.last_issued_cookie();
  }
  Registry replacement(watermark);
  Reference event;
  ASSERT_TRUE(replacement.install(node));
  EXPECT_FALSE(replacement.acquire(watermark, event));
  EXPECT_EQ(node.cookie(), watermark + 1);
  ASSERT_TRUE(replacement.retire(node));
  EXPECT_TRUE(node.reclaimable());
}

TEST(LlvmLibcNotificationRegistryTest, FailedInstallationReleasesCallerStorage) {
  Registry registry;
  Node nodes[3];
  Reference early_event;
  for (Node &node : nodes)
    ASSERT_TRUE(registry.install(node));
  ASSERT_TRUE(registry.acquire(nodes[1].cookie(), early_event));
  // Roll back a middle installation with an early terminal event, then the
  // tail and head. No record is published and no caller-owned node is hidden.
  ASSERT_TRUE(registry.retire(nodes[1]));
  ASSERT_TRUE(registry.retire(nodes[0]));
  ASSERT_TRUE(registry.retire(nodes[2]));
  EXPECT_TRUE(nodes[0].reclaimable());
  EXPECT_FALSE(nodes[1].reclaimable());
  EXPECT_TRUE(nodes[2].reclaimable());
  ASSERT_TRUE(registry.release(early_event));
  for (Node &node : nodes) {
    EXPECT_TRUE(node.reclaimable());
    EXPECT_FALSE(registry.acquire(node.cookie(), early_event));
  }
  // Retry after rollback issues new cookies, without tombstones or lost slots.
  for (Node &node : nodes) {
    ASSERT_TRUE(registry.install(node));
    EXPECT_GT(node.cookie(), uint64_t(3));
    ASSERT_TRUE(registry.publish(node));
    ASSERT_TRUE(registry.retire(node));
    EXPECT_TRUE(node.reclaimable());
  }
}

TEST(LlvmLibcNotificationRegistryTest, UnlinkKeepsOtherRegistrationsLive) {
  Registry registry;
  Node nodes[4];
  for (Node &node : nodes) {
    ASSERT_TRUE(registry.install(node));
    ASSERT_TRUE(registry.publish(node));
  }
  const unsigned order[] = {2, 3, 0, 1};
  for (unsigned index : order) {
    ASSERT_TRUE(registry.retire(nodes[index]));
    for (Node &node : nodes) {
      Reference event;
      if (node.reclaimable()) {
        EXPECT_FALSE(registry.acquire(node.cookie(), event));
      } else {
        ASSERT_TRUE(registry.acquire(node.cookie(), event));
        EXPECT_EQ(event.get(), &node);
        ASSERT_TRUE(registry.release(event));
      }
    }
  }
}
