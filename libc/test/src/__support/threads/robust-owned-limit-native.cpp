//===-- Native MMIX robust ownership limit regression ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Embed the production ownership algorithm. Kernel registration, signal masks,
// TIDs, futex waits and PI transport are modeled, not guest/kernel evidence.
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <sys/syscall.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#define LIBC_MMIX_PI_MUTEX
#include "src/__support/threads/linux/mmix/robust_mutex.cpp"

namespace M = LIBC_NAMESPACE::mmix;
using LIBC_NAMESPACE::MutexError;
using E = MutexError;
static constexpr uint32_t Tid = 123, Other = 456;
static uint32_t tid = Tid;
static uint64_t signal_mask = 0x400;
static unsigned registrations, waits, pi_acquires, pi_releases;
static int registration_error, wait_error = ETIMEDOUT;
static E pi_acquire_result = E::NONE, pi_release_result = E::NONE;

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long a1,
                                         long a2, long a3, long a4, long,
                                         long) {
  if (number == SYS_gettid)
    return tid;
  if (number == SYS_set_robust_list) {
    ++registrations;
    assert(a1 == reinterpret_cast<long>(&M::head));
    assert(a2 == sizeof(M::head));
    assert(M::head.futex_offset == -long(offsetof(M::RobustMutex, link)));
    return -registration_error;
  }
  if (number == SYS_rt_sigprocmask) {
    assert(a2 && a4 == sizeof(signal_mask));
    if (a3)
      *reinterpret_cast<uint64_t *>(a3) = signal_mask;
    auto mask = *reinterpret_cast<const uint64_t *>(a2);
    assert(a1 == 0 || a1 == 2);
    signal_mask = a1 == 0 ? signal_mask | mask : mask;
    return 0;
  }
  assert(number == LIBC_NAMESPACE::FUTEX_SYSCALL_ID);
  if (a2 == FUTEX_WAIT_BITSET) {
    assert(signal_mask == 0x400 && M::head.list_op_pending);
    assert(*reinterpret_cast<uint32_t *>(a1) == uint32_t(a3));
    ++waits;
    return -wait_error;
  }
  assert(a2 == FUTEX_WAKE && (signal_mask & M::CANCEL_MASK));
  return 0;
}

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
MutexError RobustMutex::acquire_pi(cpp::optional<Futex::Timeout>, bool) {
  ++pi_acquires;
  assert(signal_mask == 0x400);
  assert(reinterpret_cast<uintptr_t>(head.list_op_pending) ==
         (is_robust() ? (reinterpret_cast<uintptr_t>(&link) | 1) : 0));
  if (pi_acquire_result == E::NONE)
    futex.store(tid | (futex.load() & FUTEX_OWNER_DIED));
  return pi_acquire_result;
}
MutexError RobustMutex::release_pi() {
  ++pi_releases;
  assert(signal_mask & CANCEL_MASK);
  if (pi_release_result == E::NONE)
    futex.store(0);
  return pi_release_result;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

static void census(unsigned expected) {
  assert(M::owned_count == expected);
  assert(!M::head.list_op_pending && signal_mask == 0x400);
  unsigned seen = 0;
  auto *node = M::head.list.next;
  while (true) {
    node = reinterpret_cast<robust_list *>(reinterpret_cast<uintptr_t>(node) &
                                         ~uintptr_t(1));
    if (node == &M::head.list)
      break;
    assert(node && ++seen <= expected);
    node = node->next;
  }
  assert(seen == expected);
  assert(M::robust_list_empty() == (expected == 0));
}

static void rejected(M::RobustMutex &mutex, bool try_only) {
  auto word = mutex.futex.load(), flags = mutex.flags.load();
  auto *next = mutex.link.next;
  auto count = mutex.lock_count;
  auto calls = pi_acquires, slept = waits;
  assert(mutex.acquire(LIBC_NAMESPACE::cpp::nullopt, try_only) == E::OVERFLOW);
  assert(mutex.futex.load() == word && mutex.flags.load() == flags);
  assert(mutex.link.next == next && mutex.lock_count == count);
  assert(pi_acquires == calls && waits == slept);
  census(ROBUST_LIST_LIMIT);
}

int main() {
  static_assert(ROBUST_LIST_LIMIT == 2048, "Exercise the exact Linux bound");
  // Explicit stale-counter injection tests fresh initialization, not capacity.
  M::owned_count = 19;
  registration_error = EINVAL;
  M::RobustMutex candidate(false, false, false);
  assert(candidate.lock() == E::BAD_LOCK_STATE);
  assert(!M::registered && !M::owned_count && !candidate.futex.load());
  registration_error = 0;
  assert(!M::register_robust_list());
  census(0);

  struct Recursive : M::RobustMutex {
    Recursive() : RobustMutex(true, false, false) {}
  } locks[ROBUST_LIST_LIMIT - 1];
  for (auto &lock : locks)
    assert(lock.lock() == E::NONE);
  M::RobustMutex pi(false, false, false, true);
  assert(pi.lock() == E::NONE);
  census(ROBUST_LIST_LIMIT);
  M::RobustMutex stalled_pi(false, false, false, true, false);
  assert(stalled_pi.lock() == E::NONE);
  census(ROBUST_LIST_LIMIT);
  assert(stalled_pi.unlock() == E::NONE);
  census(ROBUST_LIST_LIMIT);
  auto *first = M::head.list.next;
  auto calls = registrations;
  assert(!M::register_robust_list() && registrations == calls);
  assert(!M::register_robust_list(true) && registrations == calls + 1);
  assert(M::head.list.next == first);
  census(ROBUST_LIST_LIMIT);
  registration_error = EINVAL;
  assert(M::register_robust_list(true) == EINVAL);
  registration_error = 0;
  census(ROBUST_LIST_LIMIT);

  for (bool inherit : {false, true}) {
    M::RobustMutex extra(false, false, false, inherit);
    for (uint32_t word : {uint32_t(0), uint32_t(FUTEX_OWNER_DIED), Other,
                          Other | uint32_t(FUTEX_WAITERS)}) {
      extra.futex.store(word);
      rejected(extra, false);
      rejected(extra, true);
    }
  }
  assert(locks[0].lock() == E::NONE && locks[0].lock_count == 2);
  census(ROBUST_LIST_LIMIT);
  assert(locks[0].unlock() == E::NONE && locks[0].lock_count == 1);
  census(ROBUST_LIST_LIMIT);

  pi_release_result = E::BAD_LOCK_STATE;
  assert(pi.unlock() == E::BAD_LOCK_STATE);
  assert(pi.futex.load() == Tid && pi.lock_count == 1);
  census(ROBUST_LIST_LIMIT);
  rejected(candidate, false);
  pi_release_result = E::NONE;
  assert(pi.unlock() == E::NONE);
  census(ROBUST_LIST_LIMIT - 1);
  candidate.futex.store(FUTEX_OWNER_DIED);
  assert(candidate.lock() == E::OWNER_DEAD);
  census(ROBUST_LIST_LIMIT);
  candidate.abandon_unpublished();
  assert(candidate.futex.load() == FUTEX_OWNER_DIED);
  assert(!candidate.link.next && !candidate.lock_count);
  assert(!candidate.is_inconsistent() && !candidate.is_unrecoverable());
  census(ROBUST_LIST_LIMIT - 1);
  assert(candidate.lock() == E::OWNER_DEAD);
  assert(!candidate.consistent() && candidate.unlock() == E::NONE);
  for (unsigned i = ROBUST_LIST_LIMIT - 1; i; --i)
    assert(locks[i - 1].unlock() == E::NONE);
  census(0);

  candidate.futex.store(Other);
  assert(candidate.try_lock() == E::BUSY);
  census(0);
  assert(candidate.lock() == E::TIMEOUT);
  census(0);
  wait_error = EINVAL;
  assert(candidate.lock() == E::BAD_LOCK_STATE);
  census(0);
  candidate.futex.store(0);
  tid = 0;
  assert(candidate.lock() == E::BAD_LOCK_STATE);
  tid = Tid;
  census(0);
  for (E failure : {E::BUSY, E::TIMEOUT, E::BAD_LOCK_STATE}) {
    pi_acquire_result = failure;
    assert(pi.lock() == failure && !pi.futex.load());
    census(0);
  }
  pi_acquire_result = E::NONE;
  for (auto *mutex : {&candidate, &pi}) {
    mutex->set_flag(M::MUTEX_UNRECOVERABLE, true);
    assert(mutex->lock() == E::NOT_RECOVERABLE);
    assert(!mutex->futex.load() && !mutex->link.next && !mutex->lock_count);
    census(0);
    mutex->set_flag(M::MUTEX_UNRECOVERABLE, false);
  }
  assert(candidate.lock() == E::NONE);
  candidate.abandon_unpublished();
  assert(!candidate.futex.load());
  census(0);

  assert(candidate.lock() == E::NONE);
  census(1);
  // Model the child's copied TLS and interrupted pending operation. No fork or
  // credentials are used; inherited locks must not enter the child's list.
  M::head.list_op_pending = &candidate.link;
  assert(!M::reset_robust_list_after_fork());
  census(0);
  assert(candidate.futex.load() == Tid && candidate.lock_count == 1);
  assert(pi.lock() == E::NONE);
  census(1);
  assert(pi.unlock() == E::NONE);
  census(0);
  assert(waits == 2 && pi_releases >= 4);
}
