//===-- Private growable AIO record storage --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_STORAGE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_STORAGE_H

#include "hdr/stdint_proxy.h"
#include "hdr/types/size_t.h"
#include "src/__support/CPP/new.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Private raw storage, not request ownership or a public AIO provider. The
// service serializes every pool operation/query under its state gate EXCEPT
// prepare()/dispose(), which operate on exclusively owned detached blocks
// outside the gate. There are no locks, syscalls or application callbacks here.
//
// Allocator supplies nonthrowing allocate(size, alignment) and deallocate(ptr).
// Allocation returns suitably aligned storage or nullptr; size is a multiple
// of alignment. The allocator must outlive every block it prepares and support
// concurrent prepare/dispose calls if the service makes them. Those calls do
// not access pool state. An allocator is never invoked by a gate-side method.
//
// acquire() leases raw storage, NOT a constructed T. The caller placement-
// constructs T and retains its Slot until all request/notification/etc. owners
// drain. Before release(), destroy T explicitly (or abandon an unconstructed
// lease). Neither release(), dispose() nor pool destruction calls ~T. A lease
// pins its entire block even during out-of-gate construction/destruction.
//
// Slots and live objects never move. Slot/block pointers are internal handles,
// not authenticated aiocb pointers; stale handles after release/reuse/disposal
// are invalid. No fixed record limit is imposed beyond size_t representation.
template <class T, size_t SlotsPerBlock, class Allocator> class AioStorage {
  static_assert(SlotsPerBlock != 0, "An AIO block needs at least one slot");

public:
  class Block;

  class Slot {
    friend class AioStorage;
    Block *block = nullptr;
    bool acquired = false;
    alignas(T) unsigned char data[sizeof(T)];

  public:
    LIBC_INLINE Slot() = default;
    Slot(const Slot &) = delete;
    Slot &operator=(const Slot &) = delete;

    // Only the exclusive lease holder may access this storage. Use the pointer
    // returned by placement new to access the constructed object.
    LIBC_INLINE void *storage() { return data; }
  };

private:
  static_assert(SlotsPerBlock <= SIZE_MAX / sizeof(Slot),
                "AIO slot array size must be representable");

public:
  class Block {
    friend class AioStorage;
    Allocator *allocator;
    AioStorage *owner = nullptr;
    Block *next = nullptr;
    size_t used = 0;
    Slot slots[SlotsPerBlock];

    LIBC_INLINE explicit Block(Allocator &allocator) : allocator(&allocator) {
      for (auto &slot : slots)
        slot.block = this;
    }
    LIBC_INLINE ~Block() = default;

  public:
    Block(const Block &) = delete;
    Block &operator=(const Block &) = delete;
  };

private:
  Block *head = nullptr;
  size_t capacity_value = 0;
  size_t used = 0;

public:
  LIBC_INLINE constexpr AioStorage() = default;
  AioStorage(const AioStorage &) = delete;
  AioStorage &operator=(const AioStorage &) = delete;
  LIBC_INLINE ~AioStorage() {
    // Teardown must detach and dispose explicitly, even for empty blocks.
    if (head)
      __builtin_trap();
  }

  // Outside the gate. Failure touches no pool, slot or existing block. Several
  // callers may prepare independently; publication does not assume the pool
  // still needs growth when they later acquire the gate.
  LIBC_INLINE static Block *prepare(Allocator &allocator) {
    void *memory = allocator.allocate(sizeof(Block), alignof(Block));
    return memory ? new (memory) Block(allocator) : nullptr;
  }

  // Outside the gate, with exclusive ownership of an unpublished/removed block.
  // This is also rollback for preparation that never reached publication.
  // On success the pointer and every slot pointer into the block are invalid.
  // The checks do not authorize racing disposal against a published block.
  LIBC_INLINE static bool dispose(Block *block) {
    if (!block || block->owner || block->used != 0)
      return false;
    Allocator *allocator = block->allocator;
    block->~Block();
    allocator->deallocate(block);
    return true;
  }

  // Under the gate. Success transfers exclusive block ownership to this pool;
  // failure leaves it unchanged. Both independently prepared blocks may publish
  // even if an earlier publication already provided spare slots.
  LIBC_INLINE bool publish(Block &block) {
    if (block.owner || block.used != 0 ||
        capacity_value > SIZE_MAX - SlotsPerBlock)
      return false;
    block.owner = this;
    block.next = head;
    head = &block;
    capacity_value += SlotsPerBlock;
    return true;
  }

  LIBC_INLINE size_t capacity() const { return capacity_value; }
  // Counts leases, including not-yet-constructed and already-destroyed objects.
  LIBC_INLINE size_t size() const { return used; }

  // Under the gate. Exhaustion returns nullptr; it never attempts growth.
  LIBC_INLINE Slot *acquire() {
    for (Block *block = head; block; block = block->next) {
      if (block->used == SlotsPerBlock)
        continue;
      for (auto &slot : block->slots) {
        if (slot.acquired)
          continue;
        slot.acquired = true;
        ++block->used;
        ++used;
        return &slot;
      }
    }
    return nullptr;
  }

  // Under the gate, after the caller ends T's lifetime. Never destroy a live
  // engine Request or notification merely to make this operation admissible.
  LIBC_INLINE bool release(Slot &slot) {
    if (!slot.block || slot.block->owner != this || !slot.acquired)
      return false;
    slot.acquired = false;
    --slot.block->used;
    --used;
    return true;
  }

  // Under the gate. Remove one entirely unleased block, skipping busy blocks.
  // The caller takes exclusive ownership and may dispose outside the gate or
  // publish again. No pool operation can find the block after this returns.
  LIBC_INLINE Block *detach_empty() {
    Block **link = &head;
    while (*link) {
      Block *block = *link;
      if (block->used != 0) {
        link = &block->next;
        continue;
      }
      *link = block->next;
      block->next = nullptr;
      block->owner = nullptr;
      capacity_value -= SlotsPerBlock;
      return block;
    }
    return nullptr;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_STORAGE_H
