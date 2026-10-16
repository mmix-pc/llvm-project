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
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Callers serialize every access, including admission versus retirement.
// This accounts for owners, not graph reachability, callbacks or unmapping.
// Supplied arena storage must outlive this object and every outstanding token.
class ModuleOwnership {
public:
  enum class Kind : unsigned { Handle, Dependency, Runtime };
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
  uint64_t next_ticket = 1, total = 0, counts[3] = {};
  Slot *slots = nullptr;
  State state = State::Live;
  bool permanent;

public:
  // The bound limits both live owners and lifetime ticket issuance. Exhaustion
  // never reuses a ticket; inactive storage may be reused with a fresh ticket.
  ModuleOwnership(uint64_t image, uint64_t generation, bool permanent,
                  uint64_t limit = ~uint64_t(0))
      : image(image), generation(generation), limit(limit),
        permanent(permanent) {}
  ModuleOwnership(const ModuleOwnership &) = delete;
  ModuleOwnership &operator=(const ModuleOwnership &) = delete;

  long acquire(Arena &arena, Kind kind, Token &out) {
    unsigned index = static_cast<unsigned>(kind);
    if (!image || !generation || index >= 3)
      return -EINVAL;
    if (state != State::Live)
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
        !token.ticket || index >= 3)
      return -EINVAL;
    for (Slot *slot = slots; slot; slot = slot->next) {
      if (slot->active && slot->token.ticket == token.ticket &&
          slot->token.kind == token.kind) {
        slot->active = false;
        --counts[index];
        --total;
        return 0;
      }
    }
    return -EINVAL;
  }

  long retire() {
    if (permanent || state != State::Live)
      return -EBUSY;
    state = State::Retiring;
    return 0;
  }

  // Only the coordinator may attest finalization and physical release. This
  // method does not unmap anything or authorize releasing a live callback.
  long mark_reclaimed() {
    if (state != State::Retiring || total || permanent)
      return -EBUSY;
    state = State::Reclaimed;
    return 0;
  }
  bool is_permanent() const { return permanent; }
  State admission() const { return state; }
  uint64_t owners() const { return total; }
  uint64_t owners(Kind kind) const {
    unsigned index = static_cast<unsigned>(kind);
    return index < 3 ? counts[index] : 0;
  }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
