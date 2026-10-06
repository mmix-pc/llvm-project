//===-- MMIX Linux cancellation state ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_STATE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_STATE_H

#include "src/__support/CPP/atomic.h"
#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Keep signal-visible state in one naturally aligned, lock-free octa. Only
// the owner changes enable/type; request and terminal updates preserve it.
class CancellationState {
  static constexpr uint64_t ENABLED = 1, PENDING = 2, TERMINAL = 4, ASYNC = 8;
  cpp::Atomic<uint64_t> bits{ENABLED};

public:
  constexpr CancellationState() = default;
  bool enabled() { return bits.load(cpp::MemoryOrder::ACQUIRE) & ENABLED; }
  bool ready() {
    return (bits.load(cpp::MemoryOrder::ACQUIRE) &
            (ENABLED | PENDING | TERMINAL)) == (ENABLED | PENDING);
  }
  bool terminal() { return bits.load(cpp::MemoryOrder::ACQUIRE) & TERMINAL; }
  bool asynchronous() { return bits.load(cpp::MemoryOrder::ACQUIRE) & ASYNC; }
  bool set_async(bool asynchronous) {
    auto old = bits.load(cpp::MemoryOrder::ACQUIRE);
    for (;;) {
      auto next = asynchronous ? old | ASYNC : old & ~ASYNC;
      if (bits.compare_exchange_weak(old, next, cpp::MemoryOrder::ACQ_REL))
        return old & ASYNC;
    }
  }
  bool set_enabled(bool enabled) {
    auto old = bits.load(cpp::MemoryOrder::ACQUIRE);
    for (;;) {
      auto next = enabled ? old | ENABLED : old & ~ENABLED;
      if (bits.compare_exchange_weak(old, next, cpp::MemoryOrder::ACQ_REL))
        return old & ENABLED;
    }
  }
  // Caller serializes this with lifecycle admission and terminal claiming.
  bool request() {
    auto old = bits.load(cpp::MemoryOrder::ACQUIRE);
    for (;;) {
      if (old & TERMINAL)
        return false;
      if (bits.compare_exchange_weak(old, old | PENDING,
                                     cpp::MemoryOrder::ACQ_REL))
        return true;
    }
  }
  bool claim(bool cancellation) {
    auto old = bits.load(cpp::MemoryOrder::ACQUIRE);
    for (;;) {
      if ((old & TERMINAL) ||
          (cancellation && (old & (ENABLED | PENDING)) != (ENABLED | PENDING)))
        return false;
      if (bits.compare_exchange_weak(old, old | TERMINAL,
                                     cpp::MemoryOrder::ACQ_REL))
        return true;
    }
  }
};
static_assert(sizeof(CancellationState) == 8 &&
              alignof(CancellationState) == 8);
static_assert(__atomic_always_lock_free(8, nullptr));

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
