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
    void reset_after_fork() { word.store(0, cpp::MemoryOrder::RELAXED); }
  };
  Mutex mutation, registry, arena;
  cpp::Atomic<long> mutation_owner{0};
  uint64_t mutation_depth = 0; // Accessed only by the owning thread.

public:
  // The libc fork owner already masks signals and suppresses cancellation.
  // Acquire after user prepare callbacks and before any pthread registry lock.
  long prepare_fork();
  void finish_fork(bool child);
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
    ~Session() noexcept(false);
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    long error() const { return failure; }
    auto initializer_barrier() const { return cancellation.invoke_initializer; }
    auto finalizer_barrier() const { return cancellation.invoke_finalizer; }
    // A callback boundary must retain mutation serialization and cancellation
    // suppression, but never a short registry/arena guard.
    long callback_owner() const {
      return !failure && suppressed && top && rank == Kind::Mutation ? tid : 0;
    }
    // Detached plans retain their code explicitly and invoke outside all
    // loader guards, including a mutation guard held by an outer session.
    long unlocked_callback_owner() const {
      return !failure && suppressed && !top && rank == Kind::None &&
                     locks.mutation_owner.load() != tid
                 ? tid
                 : 0;
    }
    class Callback {
      Session &session;

    public:
      explicit Callback(Session &session, bool unlocked = false)
          : session(session) {
        if (!(unlocked ? session.unlocked_callback_owner()
                       : session.callback_owner()) ||
            bool(session.cancellation.begin_callback) !=
                bool(session.cancellation.end_callback))
          __builtin_trap();
        if (session.cancellation.begin_callback)
          session.cancellation.begin_callback();
      }
      ~Callback() {
        if (session.cancellation.end_callback)
          session.cancellation.end_callback();
      }
      Callback(const Callback &) = delete;
      Callback &operator=(const Callback &) = delete;
    };
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
