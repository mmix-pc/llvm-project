//===-- Checked module ownership ---------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_MODULE_OWNERSHIP_H
#define LLVM_LIBC_LOADER_LINUX_MODULE_OWNERSHIP_H

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "loader/linux/arena.h"
#include "loader/linux/retirement_events.h"
#include "src/__support/CPP/new.h"
#include "src/__support/CPP/atomic.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Callers serialize ticket and state-transition operations. Signal retention
// alone is lock-free and races retirement through its atomic admission gate.
// This accounts for owners, not graph reachability, callbacks or unmapping.
// Supplied arena storage must outlive this object and every outstanding token.
class ModuleOwnership {
public:
  // Process callbacks are drained by their owner's finalization, not external
  // roots that prevent that finalization. All tickets still prevent unmapping.
  enum class Kind : unsigned { Handle, Dependency, Runtime, ProcessCallback };
  enum class State { Live, Retiring, Reclaimed };
  struct Token {
    uint64_t image = 0, generation = 0, ticket = 0;
    Kind kind = Kind::Handle;
  };

private:
  struct Slot {
    Token token;
    Slot *next;
    bool active;
  };
  uint64_t image, generation, limit;
  uint64_t next_ticket = 1, total = 0, counts[4] = {};
  Slot *slots = nullptr;
  State state = State::Live;
  bool permanent;
  bool finalization_started = false;
  RetirementEvents *events;
  // Signal-side registration cannot allocate tickets or take the writer lock.
  // Bit 0 closes admission, bit 1 is a permanent signal-code pin, higher bits
  // count temporary registration leases. Retirement races use the same CAS.
  mutable cpp::Atomic<uint64_t> signal_owners{0};

public:
  // The bound limits both live owners and lifetime ticket issuance. Exhaustion
  // never reuses a ticket; inactive storage may be reused with a fresh ticket.
  ModuleOwnership(uint64_t image, uint64_t generation, bool permanent,
                  uint64_t limit = ~uint64_t(0),
                  RetirementEvents *events = nullptr)
      : image(image), generation(generation), limit(limit),
        permanent(permanent), events(events) {}
  ModuleOwnership(const ModuleOwnership &) = delete;
  ModuleOwnership &operator=(const ModuleOwnership &) = delete;

  long acquire(Arena &arena, Kind kind, Token &out) {
    unsigned index = static_cast<unsigned>(kind);
    if (!image || !generation || index >= 4)
      return -EINVAL;
    if (state != State::Live)
      return -EBUSY;
    if (finalization_started &&
        (kind == Kind::Handle || kind == Kind::Dependency))
      return -EBUSY;
    if (!next_ticket || next_ticket > limit || total >= limit)
      return -EOVERFLOW;
    Slot *slot = slots;
    while (slot && slot->active)
      slot = slot->next;
    if (!slot) {
      void *storage;
      long error = arena.allocate(sizeof(Slot), alignof(Slot), storage);
      if (error)
        return error;
      slot = new (storage) Slot{{}, slots, false};
      slots = slot;
    }
    slot->token = {image, generation, next_ticket, kind};
    slot->active = true;
    next_ticket = next_ticket == ~uint64_t(0) ? 0 : next_ticket + 1;
    ++counts[index];
    ++total;
    out = slot->token;
    return 0;
  }

  long release(const Token &token) {
    unsigned index = static_cast<unsigned>(token.kind);
    if (token.image != image || token.generation != generation ||
        !token.ticket || index >= 4)
      return -EINVAL;
    for (Slot *slot = slots; slot; slot = slot->next) {
      if (slot->active && slot->token.ticket == token.ticket &&
          slot->token.kind == token.kind) {
        slot->active = false;
        --counts[index];
        --total;
        if (events)
          events->request();
        return 0;
      }
    }
    return -EINVAL;
  }

  long retire() {
    if (permanent || state != State::Live)
      return -EBUSY;
    uint64_t expected = 0;
    if (!signal_owners.compare_exchange_strong(expected, 1))
      return -EBUSY;
    state = State::Retiring;
    return 0;
  }
  // Roll back a writer's unpublished multi-image retirement reservation.
  void cancel_retirement() {
    if (state != State::Retiring || signal_owners.load() != 1)
      __builtin_trap();
    state = State::Live;
    signal_owners.store(0);
  }
  long retain_signal() {
    static_assert(__atomic_always_lock_free(sizeof(uint64_t), nullptr));
    uint64_t value = signal_owners.load();
    for (;;) {
      if (value & 1)
        return -EBUSY;
      if (value > ~uint64_t(0) - 4)
        return -EOVERFLOW;
      if (signal_owners.compare_exchange_weak(value, value + 4))
        return 0;
    }
  }
  void finish_signal(bool installed) {
    if (installed)
      signal_owners.fetch_or(2);
    if (signal_owners.fetch_sub(4) < 4)
      __builtin_trap();
    if (events)
      events->request();
  }

  // Only the coordinator may attest finalization and physical release. This
  // method does not unmap anything or authorize releasing a live callback.
  long mark_reclaimed() {
    if (state != State::Retiring || total || permanent)
      return -EBUSY;
    state = State::Reclaimed;
    slots = nullptr; // Inactive ticket storage can now be reclaimed.
    return 0;
  }
  bool is_permanent() const { return permanent || (signal_owners.load() & 2); }
  // Writer-serialized successful RTLD_NODELETE admission is irreversible.
  void pin() { permanent = true; }
  // Writer-serialized and irreversible for this image lifetime. Runtime
  // callbacks may still retain code, but a load must never resurrect it.
  bool available_for_load() const {
    return state == State::Live && !finalization_started;
  }
  void begin_finalization() {
    if (!available_for_load())
      __builtin_trap();
    finalization_started = true;
  }
  State admission() const { return state; }
  uint64_t owners() const {
    uint64_t transient = signal_owners.load() >> 2;
    return transient > ~uint64_t(0) - total ? ~uint64_t(0) : total + transient;
  }
  uint64_t owners(Kind kind) const {
    unsigned index = static_cast<unsigned>(kind);
    return index < 4 ? counts[index] : 0;
  }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
