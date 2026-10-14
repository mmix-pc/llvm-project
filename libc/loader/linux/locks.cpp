//===-- Loader synchronization --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/locks.h"
#include "hdr/errno_macros.h"
#include "loader/linux/syscall.h"
#include <linux/futex.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

long LoaderLocks::Mutex::lock() {
  uint32_t expected = 0;
  if (word.compare_exchange_strong(expected, 1, cpp::MemoryOrder::ACQUIRE,
                                   cpp::MemoryOrder::RELAXED))
    return 0;
  // The same 0/1/2 protocol as RawMutex, without its libc/time dependencies.
  while (word.exchange(2, cpp::MemoryOrder::ACQUIRE) != 0) {
    long result = raw_syscall(SYS_futex, reinterpret_cast<long>(&word),
                              FUTEX_WAIT_PRIVATE, 2);
    if (result < 0 && result != -EINTR && result != -EAGAIN)
      return result;
  }
  return 0;
}

void LoaderLocks::Mutex::unlock() {
  uint32_t previous = word.exchange(0, cpp::MemoryOrder::RELEASE);
  if (!previous || previous > 2)
    __builtin_trap();
  if (previous == 2 && raw_syscall(SYS_futex, reinterpret_cast<long>(&word),
                                   FUTEX_WAKE_PRIVATE, 1) < 0)
    __builtin_trap();
}

LoaderLocks::Session::Session(LoaderLocks &owner, LoaderCancellation hooks)
    : locks(owner), cancellation(hooks) {
  if (!hooks.enter || !hooks.leave) {
    failure = -EINVAL;
    return;
  }
  failure = cancellation.enter(&saved);
  if (failure)
    return;
  suppressed = true;
  tid = raw_syscall(SYS_gettid);
  if (tid <= 0)
    failure = tid < 0 ? tid : -EINVAL;
}

LoaderLocks::Session::~Session() {
  if (top)
    __builtin_trap();
  if (suppressed && cancellation.leave(saved))
    __builtin_trap();
}

LoaderLocks::Guard::Guard(Session &scope, Kind requested)
    : session(scope), kind(requested), previous_rank(scope.rank),
      failure(scope.error()) {
  if (failure)
    return;
  if (kind == Kind::None || kind > Kind::Arena || kind < previous_rank ||
      (kind == previous_rank && kind != Kind::Mutation)) {
    failure = -EDEADLK;
    return;
  }
  LoaderLocks &locks = session.locks;
  if (kind == Kind::Mutation) {
    if (locks.mutation_owner.load(cpp::MemoryOrder::ACQUIRE) == session.tid) {
      if (locks.mutation_depth == ~uint64_t(0)) {
        failure = -EOVERFLOW;
        return;
      }
      ++locks.mutation_depth;
    } else {
      failure = locks.mutation.lock();
      if (failure)
        return;
      locks.mutation_depth = 1;
      locks.mutation_owner.store(session.tid, cpp::MemoryOrder::RELEASE);
    }
  } else {
    failure = (kind == Kind::Registry ? locks.registry : locks.arena).lock();
    if (failure)
      return;
  }
  previous = session.top;
  session.top = this;
  session.rank = kind;
  held = true;
}

LoaderLocks::Guard::~Guard() {
  if (!held)
    return;
  if (session.top != this)
    __builtin_trap();
  LoaderLocks &locks = session.locks;
  if (kind == Kind::Mutation) {
    if (locks.mutation_owner.load(cpp::MemoryOrder::RELAXED) != session.tid ||
        !locks.mutation_depth)
      __builtin_trap();
    if (--locks.mutation_depth == 0) {
      locks.mutation_owner.store(0, cpp::MemoryOrder::RELEASE);
      locks.mutation.unlock();
    }
  } else {
    (kind == Kind::Registry ? locks.registry : locks.arena).unlock();
  }
  session.top = previous;
  session.rank = previous_rank;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
