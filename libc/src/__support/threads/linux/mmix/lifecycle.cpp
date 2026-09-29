//===-- MMIX Linux thread lifetime primitives
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/linux/mmix/lifecycle.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

LIBC_CONSTINIT ThreadRegistry thread_registry;

ThreadRegistryLock::ThreadRegistryLock(ThreadRegistry &r) : registry(r) {
  registry.mutex.value.lock();
}

ThreadRegistryLock::~ThreadRegistryLock() {
  if (changed)
    registry.event.value.fetch_add(1, cpp::MemoryOrder::RELEASE);
  registry.mutex.value.unlock();
  // A last-pin release can make a control freeable immediately after unlock.
  // Only process-lifetime memory may be touched here.
  if (changed)
    registry.event.value.notify_all();
}

bool ThreadRegistryLock::belongs(const ThreadControl &c) const {
  return c.registry == &registry && c.lifecycle_pin;
}

uint32_t ThreadRegistryLock::sequence() {
  return registry.event.value.load(cpp::MemoryOrder::ACQUIRE);
}

bool ThreadRegistryLock::reserve() {
  if (registry.exiting || registry.reservations == SIZE_MAX)
    return false;
  ++registry.reservations;
  change();
  return true;
}

bool ThreadRegistryLock::cancel_reservation() {
  if (!registry.reservations)
    return false;
  --registry.reservations;
  change();
  return true;
}

bool ThreadRegistryLock::insert(ThreadControl &c, bool detached, bool helper) {
  if (c.registry || c.owner != ThreadOwner::Unpublished || !c.creator_pin ||
      !c.lifecycle_pin || registry.exiting ||
      (!helper && (!registry.reservations || registry.live == SIZE_MAX)))
    return false;
  c.registry = &registry;
  c.owner = detached ? ThreadOwner::Detached : ThreadOwner::Joinable;
  c.listed = true;
  c.leases_open = true;
  c.counted = !helper;
  c.next = registry.head;
  registry.head = &c;
  if (!helper) {
    --registry.reservations;
    ++registry.live;
  }
  change();
  return true;
}

bool ThreadRegistryLock::adopt_abort(ThreadControl &c) {
  if (c.registry || c.owner != ThreadOwner::Unpublished || !c.creator_pin ||
      !c.lifecycle_pin)
    return false;
  c.registry = &registry;
  c.owner = ThreadOwner::AbortOwner;
  change();
  return true;
}

ThreadControl *ThreadRegistryLock::pin(ThreadAttributes *handle) {
  for (ThreadControl *c = registry.head; c; c = c->next) {
    if (&c->attributes != handle)
      continue;
    if (c->api_pins == SIZE_MAX)
      return nullptr;
    ++c->api_pins;
    return c;
  }
  return nullptr;
}

bool ThreadRegistryLock::unpin(ThreadControl &c) {
  if (!belongs(c) || !c.api_pins)
    return false;
  if (--c.api_pins == 0)
    change();
  return true;
}

bool ThreadRegistryLock::drop_creator(ThreadControl &c) {
  if (!belongs(c) || !c.creator_pin)
    return false;
  c.creator_pin = false;
  change();
  return true;
}

bool ThreadRegistryLock::claim_join(ThreadControl &c) {
  if (!belongs(c) || !c.api_pins || c.owner != ThreadOwner::Joinable)
    return false;
  c.owner = ThreadOwner::JoinOwner;
  change();
  return true;
}

bool ThreadRegistryLock::release_join(ThreadControl &c) {
  if (!belongs(c) || c.owner != ThreadOwner::JoinOwner)
    return false;
  c.owner = ThreadOwner::Joinable;
  change();
  return true;
}

bool ThreadRegistryLock::detach(ThreadControl &c) {
  if (!belongs(c) || !c.api_pins || c.owner != ThreadOwner::Joinable)
    return false;
  c.owner = ThreadOwner::Detached;
  change();
  return true;
}

bool ThreadRegistryLock::start(ThreadControl &c) {
  if (!belongs(c) || !c.listed || c.execution != ThreadExecution::Starting)
    return false;
  c.execution = ThreadExecution::Running;
  return true;
}

bool ThreadRegistryLock::begin_cleanup(ThreadControl &c) {
  if (!belongs(c) || c.execution != ThreadExecution::Running)
    return false;
  c.execution = ThreadExecution::Cleaning;
  return true;
}

bool ThreadRegistryLock::exit_ready(ThreadControl &c,
                                    ThreadReturnValue result) {
  if (!belongs(c) || c.execution != ThreadExecution::Cleaning ||
      c.leases_open || c.leases)
    return false;
  c.attributes.retval = result;
  c.execution = ThreadExecution::ExitReady;
  if (c.counted) {
    --registry.live;
    c.counted = false;
  }
  change();
  return true;
}

bool ThreadRegistryLock::acquire_lease(ThreadControl &c) {
  if (!belongs(c) || !c.api_pins || !c.leases_open || !c.listed ||
      c.leases == SIZE_MAX)
    return false;
  ++c.leases;
  return true;
}

bool ThreadRegistryLock::release_lease(ThreadControl &c) {
  if (!belongs(c) || !c.leases)
    return false;
  if (--c.leases == 0)
    change();
  return true;
}

bool ThreadRegistryLock::close_leases(ThreadControl &c) {
  if (!belongs(c))
    return false;
  c.leases_open = false;
  change();
  return true;
}

bool ThreadRegistryLock::begin_reaping(ThreadControl &c) {
  if (!belongs(c) || c.leases_open || c.leases ||
      c.clear_tid.value.load(cpp::MemoryOrder::ACQUIRE) != 0)
    return false;
  if (c.owner != ThreadOwner::AbortOwner &&
      ((c.owner != ThreadOwner::JoinOwner &&
        c.owner != ThreadOwner::Detached) ||
       c.execution != ThreadExecution::ExitReady))
    return false;
  if (c.listed) {
    ThreadControl **link = &registry.head;
    while (*link != &c)
      link = &(*link)->next;
    *link = c.next;
    c.next = nullptr;
    c.listed = false;
  }
  c.owner = ThreadOwner::Reaping;
  change();
  return true;
}

bool ThreadRegistryLock::finish_reaping(ThreadControl &c) {
  if (!belongs(c) || c.owner != ThreadOwner::Reaping || c.creator_pin ||
      c.api_pins || c.leases)
    return false;
  c.owner = ThreadOwner::Reaped;
  c.lifecycle_pin = false;
  change();
  // The exclusive reclaimer copies descriptors before this transfer. It may
  // unmap only after unlocking, and must retain failed-unmap ownership itself.
  return true;
}

bool ThreadRegistryLock::claim_process_exit() {
  if (registry.exiting || registry.live || registry.reservations)
    return false;
  registry.exiting = true;
  change();
  return true;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
