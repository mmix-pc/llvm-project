//===-- Loader synchronization -----------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_LOCKS_H
#define LLVM_LIBC_LOADER_LINUX_LOCKS_H

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/OSUtil/linux/loader_services.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

class LoaderLocks {
  class Mutex {
    cpp::Atomic<uint32_t> word{0};

  public:
    long lock();
    void unlock();
  };
  Mutex mutation, registry, arena;
  cpp::Atomic<long> mutation_owner{0};
  uint64_t mutation_depth = 0; // Accessed only by the owning thread.

public:
  enum class Kind { None, Mutation, Registry, Arena };
  class Guard;
  class Session {
    friend class Guard;
    LoaderLocks &locks;
    LoaderCancellation cancellation;
    uint64_t saved = 0;
    long tid = 0, failure = 0;
    bool suppressed = false;
    Kind rank = Kind::None;
    Guard *top = nullptr;

  public:
    Session(LoaderLocks &locks, LoaderCancellation cancellation);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    long error() const { return failure; }
  };

  class Guard {
    Session &session;
    Kind kind, previous_rank;
    Guard *previous = nullptr;
    bool held = false;
    long failure;

  public:
    Guard(Session &session, Kind kind);
    ~Guard();
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
    long error() const { return failure; }
  };

  // Mutation may span user constructors, with same-thread recursion. Registry
  // and arena guards must not span callbacks, public allocation or unwind.
  // Nested guards share their Session and release in reverse order. A nested
  // loader API may create a new Session only outside registry/arena guards.
  // No loader lock may be acquired under the pthread registry lock.
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
