//===-- Notification cookie ownership ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_NOTIFICATION_REGISTRY_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_NOTIFICATION_REGISTRY_H

#include "hdr/stdint_proxy.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Caller-serialized intrusive ownership only: no allocation, locking, callback
// invocation or automatic destruction. The caller must keep the registry alive
// until all registrations and event references drain, and keep nodes alive until
// reclaimable(). EventReference storage must survive until explicit release.
//
// Cookies are scoped to this registry. Route transport events to their owning
// registry, and preserve that registry across service restarts. Independent
// registries may issue the same numeric cookie; they never share node ownership.
class NotificationRegistry {
public:
  class Node {
    friend class NotificationRegistry;
    enum class State { Inactive, Installing, Live, Retired };

    NotificationRegistry *owner = nullptr;
    Node *previous = nullptr;
    Node *next = nullptr;
    uint64_t cookie_value = 0;
    uint64_t events = 0;
    State state = State::Inactive;

  public:
    LIBC_INLINE constexpr Node() = default;
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;

    LIBC_INLINE uint64_t cookie() const { return cookie_value; }
    LIBC_INLINE bool installing() const { return state == State::Installing; }
    LIBC_INLINE bool live() const { return state == State::Live; }
    LIBC_INLINE bool reclaimable() const { return owner == nullptr; }
  };

  // An explicit, noncopyable reference prevents a duplicate release from
  // consuming another event's ownership. An empty reference can be reused.
  class EventReference {
    friend class NotificationRegistry;
    Node *node = nullptr;

  public:
    LIBC_INLINE constexpr EventReference() = default;
    EventReference(const EventReference &) = delete;
    EventReference &operator=(const EventReference &) = delete;

    LIBC_INLINE Node *get() const { return node; }
  };

private:
  Node *head = nullptr;
  uint64_t last_cookie;

  LIBC_INLINE static void drain(Node &node) {
    if (node.state == Node::State::Retired && node.events == 0) {
      node.owner = nullptr;
      node.state = Node::State::Inactive;
    }
  }

public:
  // The watermark permits deterministic exhaustion tests or restoration of a
  // drained registry. A replacement must not lower its predecessor's watermark.
  LIBC_INLINE explicit constexpr NotificationRegistry(
      uint64_t last_issued_cookie = 0)
      : last_cookie(last_issued_cookie) {}
  NotificationRegistry(const NotificationRegistry &) = delete;
  NotificationRegistry &operator=(const NotificationRegistry &) = delete;

  LIBC_INLINE uint64_t last_issued_cookie() const { return last_cookie; }

  // Installing owns one registration reference. Failed admission changes
  // neither the node nor the cookie counter. Retiring a failed installation
  // consumes its cookie permanently, even when the same node is reused.
  LIBC_INLINE bool install(Node &node) {
    if (!node.reclaimable() || last_cookie == UINT64_MAX)
      return false;
    node.owner = this;
    node.cookie_value = ++last_cookie;
    node.state = Node::State::Installing;
    node.previous = nullptr;
    node.next = head;
    if (head != nullptr)
      head->previous = &node;
    head = &node;
    return true;
  }

  LIBC_INLINE bool publish(Node &node) {
    if (node.owner != this || !node.installing())
      return false;
    node.state = Node::State::Live;
    return true;
  }

  // Also accepts Installing nodes so intake can preserve an early MQ terminal
  // event. The caller must defer delivery until publication and discard that
  // event on installation failure. This function establishes ownership only.
  LIBC_INLINE bool acquire(uint64_t cookie, EventReference &reference) {
    if (cookie == 0 || reference.node != nullptr)
      return false;
    for (Node *node = head; node != nullptr; node = node->next) {
      if (node->cookie_value != cookie)
        continue;
      if (node->events == UINT64_MAX)
        return false;
      ++node->events;
      reference.node = node;
      return true;
    }
    return false;
  }

  // An accepted event may give callback resources their own reference after
  // retirement. This does not reopen cookie lookup or revive registration.
  LIBC_INLINE bool retain(const EventReference &source,
                          EventReference &reference) {
    Node *node = source.node;
    if (reference.node != nullptr || node == nullptr || node->owner != this ||
        node->events == 0 || node->events == UINT64_MAX)
      return false;
    ++node->events;
    reference.node = node;
    return true;
  }

  // Stop new acquisitions and drop registration ownership without waiting for
  // accepted events. Wrong-registry, inactive and repeated retirement fail
  // without changing ownership. The caller may reclaim only when reclaimable().
  LIBC_INLINE bool retire(Node &node) {
    if (node.owner != this || (!node.installing() && !node.live()))
      return false;
    if (node.previous != nullptr)
      node.previous->next = node.next;
    else
      head = node.next;
    if (node.next != nullptr)
      node.next->previous = node.previous;
    node.previous = nullptr;
    node.next = nullptr;
    node.state = Node::State::Retired;
    drain(node);
    return true;
  }

  // Successful release empties the token, even while other references remain.
  // A wrong registry or empty token fails without consuming any reference.
  LIBC_INLINE bool release(EventReference &reference) {
    Node *node = reference.node;
    if (node == nullptr || node->owner != this || node->events == 0)
      return false;
    reference.node = nullptr;
    --node->events;
    drain(*node);
    return true;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_NOTIFICATION_REGISTRY_H
