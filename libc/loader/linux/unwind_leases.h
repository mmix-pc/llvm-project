//===-- Checked unwind snapshot leases ---------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_UNWIND_LEASES_H
#define LLVM_LIBC_LOADER_LINUX_UNWIND_LEASES_H

#include "loader/linux/snapshot_storage.h"
#include "src/__support/OSUtil/linux/loader_services.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Each token has one owner. Copying requires retain(); raw structure copies
// are borrows. Operations on one owner must not race its release. Slots and
// tokens are never dereferenced using caller-provided addresses. Control blocks
// live until process exit; snapshot storage is released by the final owner.
template <class Snapshot> class UnwindLeases {
  static_assert(__atomic_always_lock_free(sizeof(uintptr_t), nullptr));
  using Lease = typename SnapshotStorage<Snapshot>::Lease;
  static constexpr uintptr_t Busy = UINTPTR_MAX;
  struct Slot {
    cpp::Atomic<uintptr_t> token{0};
    void *snapshot = nullptr;
    UnwindImageView view{};
  };
  struct Block {
    Block *next;
    Slot slots[64];
    explicit Block(Block *next) : next(next) {}
  };
  Arena controls;
  cpp::Atomic<Block *> blocks{nullptr};
  cpp::Atomic<uintptr_t> sequence{1};

  Slot *claim(uintptr_t token) {
    if (!token || token == Busy)
      return nullptr;
    for (Block *b = blocks.load(); b; b = b->next)
      for (auto &slot : b->slots) {
        uintptr_t expected = token;
        if (slot.token.compare_exchange_strong(expected, Busy))
          return &slot;
      }
    return nullptr;
  }
  long publish(void *snapshot, const UnwindImageView &view,
               UnwindImageView &out) {
    for (Block *b = blocks.load(); b; b = b->next)
      for (auto &slot : b->slots) {
        uintptr_t empty = 0;
        if (!slot.token.compare_exchange_strong(empty, Busy))
          continue;
        uintptr_t token = sequence.load();
        do {
          if (token == Busy) {
            slot.token.store(0);
            return EOVERFLOW;
          }
        } while (!sequence.compare_exchange_weak(token, token + 1));
        slot.snapshot = snapshot;
        slot.view = view;
        slot.view.token = token;
        out = slot.view;
        slot.token.store(token);
        return 0;
      }
    return EAGAIN;
  }

public:
  class Reservation {
    friend class UnwindLeases;
    Arena arena;
    Block *block = nullptr;

  public:
    explicit Reservation(size_t page) : arena(page) {}
    long abort() {
      block = nullptr;
      return arena.reset();
    }
  };
  explicit UnwindLeases(size_t page) : controls(page) {}
  // Writer-only preparation. Reserve at least one block of free capacity;
  // readers never grow it. Exhaustion is an explicit EAGAIN, never a raw view.
  long prepare(Reservation &reservation) {
    if (reservation.block)
      return 0;
    size_t free = 0;
    for (Block *b = blocks.load(); b; b = b->next)
      for (auto &slot : b->slots)
        free += slot.token.load() == 0;
    if (free >= 64)
      return 0;
    void *memory;
    long error =
        reservation.arena.allocate(sizeof(Block), alignof(Block), memory);
    if (error)
      return -error;
    reservation.block = new (memory) Block(nullptr);
    return 0;
  }
  bool can_publish(const Reservation &reservation) const {
    return controls.can_adopt(reservation.arena);
  }
  void publish(Reservation &reservation) {
    if (!controls.adopt(reservation.arena))
      __builtin_trap();
    if (reservation.block) {
      reservation.block->next = blocks.load();
      blocks.store(reservation.block);
      reservation.block = nullptr;
    }
  }
  long acquire(Lease &lease, const UnwindImageView &view,
               UnwindImageView &out) {
    out = {};
    if (!lease.get())
      return EINVAL;
    void *snapshot = lease.detach();
    long error = publish(snapshot, view, out);
    if (error)
      Lease::release_detached(snapshot);
    return error;
  }
  long retain(uintptr_t token, UnwindImageView &out) {
    out = {};
    Slot *slot = claim(token);
    if (!slot)
      return EINVAL;
    Lease::retain_detached(slot->snapshot);
    long error = publish(slot->snapshot, slot->view, out);
    if (error)
      Lease::release_detached(slot->snapshot);
    slot->token.store(token);
    return error;
  }
  long release(uintptr_t token) {
    Slot *slot = claim(token);
    if (!slot)
      return EINVAL;
    Lease::release_detached(slot->snapshot);
    slot->snapshot = nullptr;
    slot->view = {};
    slot->token.store(0);
    return 0;
  }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
