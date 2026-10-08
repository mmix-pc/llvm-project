//===-- MMIX Linux robust mutex ownership --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "robust_mutex.h"
#include "cancellation_point.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/threads/identifier.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
LIBC_CONSTINIT LIBC_THREAD_LOCAL robust_list_head head{};
LIBC_CONSTINIT LIBC_THREAD_LOCAL bool registered = false;

// Block only the runtime cancellation signal. Ordinary application handlers
// remain deliverable; they must not use pthread mutex APIs (not signal-safe).
class Operation {
  uint64_t previous = 0;
  bool blocked = true;
  robust_list *node;

public:
  explicit Operation(robust_list *entry) : node(entry) {
    uint64_t mask = CANCEL_MASK;
    if (syscall_impl<long>(SYS_rt_sigprocmask, 0, &mask, &previous,
                           sizeof(mask)))
      __builtin_trap();
    __atomic_store_n(&head.list_op_pending, node, __ATOMIC_SEQ_CST);
  }
  ~Operation() {
    resume();
    __atomic_store_n(&head.list_op_pending, nullptr, __ATOMIC_SEQ_CST);
    if (syscall_impl<long>(SYS_rt_sigprocmask, 2, &previous, 0,
                           sizeof(previous)))
      __builtin_trap();
  }
  void pause() {
    blocked = false;
    if (syscall_impl<long>(SYS_rt_sigprocmask, 2, &previous, 0,
                           sizeof(previous)))
      __builtin_trap();
  }
  void resume() {
    if (blocked)
      return;
    uint64_t mask = CANCEL_MASK;
    if (syscall_impl<long>(SYS_rt_sigprocmask, 0, &mask, 0, sizeof(mask)))
      __builtin_trap();
    __atomic_store_n(&head.list_op_pending, node, __ATOMIC_SEQ_CST);
    blocked = true;
  }
};

void insert(RobustMutex &mutex) {
  mutex.link.next = head.list.next;
  auto *tagged = reinterpret_cast<robust_list *>(
      reinterpret_cast<uintptr_t>(&mutex.link) | mutex.priority_inherit);
  __atomic_store_n(&head.list.next, tagged, __ATOMIC_SEQ_CST);
}

void remove(RobustMutex &mutex) {
  robust_list *previous = &head.list;
  while ((reinterpret_cast<uintptr_t>(previous->next) & ~uintptr_t(1)) !=
         reinterpret_cast<uintptr_t>(&mutex.link)) {
    previous = reinterpret_cast<robust_list *>(
        reinterpret_cast<uintptr_t>(previous->next) & ~uintptr_t(1));
    if (!previous || previous == &head.list)
      __builtin_trap();
  }
  __atomic_store_n(&previous->next, mutex.link.next, __ATOMIC_SEQ_CST);
  mutex.link.next = nullptr;
}
} // namespace

int register_robust_list(bool force) {
  if (registered && !force)
    return 0;
  if (!registered) {
    head.list.next = &head.list;
    head.futex_offset = -long(__builtin_offsetof(RobustMutex, link));
    head.list_op_pending = nullptr;
  }
  long result = syscall_impl<long>(SYS_set_robust_list, &head, sizeof(head));
  if (result < 0)
    return int(-result);
  registered = true;
  return 0;
}

MutexError RobustMutex::acquire(cpp::optional<Futex::Timeout> timeout,
                                bool try_only) {
  if (register_robust_list())
    return MutexError::BAD_LOCK_STATE;
  auto *pending =
      robust ? reinterpret_cast<robust_list *>(
                   reinterpret_cast<uintptr_t>(&link) | priority_inherit)
             : nullptr;
  Operation operation(pending);
  uint32_t tid = uint32_t(internal::gettid());
  if (!tid || (tid & ~FUTEX_TID_MASK))
    return MutexError::BAD_LOCK_STATE;
  uint32_t value = futex.load(cpp::MemoryOrder::RELAXED);
  for (;;) {
    if ((value & FUTEX_TID_MASK) == tid) {
      if (recursive) {
        if (lock_count == cpp::numeric_limits<size_t>::max())
          return MutexError::OVERFLOW;
        ++lock_count;
        return MutexError::NONE;
      }
      if (try_only)
        return MutexError::BUSY;
      if (error_checking)
        return MutexError::DEADLOCK;
    }
#ifdef LIBC_MMIX_PI_MUTEX
    if (priority_inherit) {
      operation.pause();
      MutexError result = acquire_pi(timeout, try_only);
      operation.resume();
      if (result != MutexError::NONE)
        return result;
      value = futex.load(cpp::MemoryOrder::RELAXED);
      if (unrecoverable) {
        if (release_pi() != MutexError::NONE)
          __builtin_trap();
        return MutexError::NOT_RECOVERABLE;
      }
      bool died = value & FUTEX_OWNER_DIED;
      if (died)
        futex.fetch_and(~uint32_t(FUTEX_OWNER_DIED), cpp::MemoryOrder::ACQUIRE);
      inconsistent = died;
      lock_count = 1;
      if (robust)
        insert(*this);
      return died ? MutexError::OWNER_DEAD : MutexError::NONE;
    }
#endif
    if (!(value & FUTEX_TID_MASK)) {
      uint32_t desired = tid | (value & FUTEX_WAITERS);
      bool died = value & FUTEX_OWNER_DIED;
      if (!futex.compare_exchange_strong(value, desired,
                                         cpp::MemoryOrder::ACQUIRE,
                                         cpp::MemoryOrder::RELAXED))
        continue;
      if (unrecoverable) {
        uint32_t old = futex.exchange(0, cpp::MemoryOrder::RELEASE);
        if (old & FUTEX_WAITERS)
          futex.notify_all(/*is_shared=*/true);
        return MutexError::NOT_RECOVERABLE;
      }
      inconsistent = died;
      lock_count = 1;
      insert(*this);
      return died ? MutexError::OWNER_DEAD : MutexError::NONE;
    }
    if (try_only)
      return MutexError::BUSY;
    if (!(value & FUTEX_WAITERS)) {
      if (!futex.compare_exchange_strong(value, value | FUTEX_WAITERS,
                                         cpp::MemoryOrder::RELAXED))
        continue;
      value |= FUTEX_WAITERS;
    }
    // Pending identifies an unowned candidate while waiting. Do not defer
    // asynchronous cancellation indefinitely behind another owner.
    operation.pause();
    // Linux robust exit always wakes shared keys, even for a private mutex.
    auto result = futex.wait(value, timeout, /*is_shared=*/true);
    operation.resume();
    if (!result.has_value() && result.error() == ETIMEDOUT)
      return MutexError::TIMEOUT;
    if (!result.has_value() && result.error() != EINTR &&
        result.error() != EAGAIN)
      return MutexError::BAD_LOCK_STATE;
    value = futex.load(cpp::MemoryOrder::RELAXED);
  }
}

MutexError RobustMutex::unlock() {
  auto *pending =
      robust ? reinterpret_cast<robust_list *>(
                   reinterpret_cast<uintptr_t>(&link) | priority_inherit)
             : nullptr;
  Operation operation(pending);
  if ((futex.load(cpp::MemoryOrder::RELAXED) & FUTEX_TID_MASK) !=
      uint32_t(internal::gettid()))
    return MutexError::UNLOCK_WITHOUT_LOCK;
  if (recursive && lock_count > 1) {
    --lock_count;
    return MutexError::NONE;
  }
  if (robust)
    remove(*this);
  bool poisoned = unrecoverable;
  if (inconsistent)
    unrecoverable = 1;
  lock_count = 0;
#ifdef LIBC_MMIX_PI_MUTEX
  if (priority_inherit) {
    MutexError result = release_pi();
    if (result != MutexError::NONE) {
      // Failure did not hand off ownership; restore the registered state.
      lock_count = 1;
      unrecoverable = poisoned;
      if (robust)
        insert(*this);
    }
    return result;
  }
#else
  (void)poisoned;
#endif
  uint32_t old = futex.exchange(0, cpp::MemoryOrder::RELEASE);
  if (old & FUTEX_WAITERS)
    futex.notify_all(/*is_shared=*/true);
  return MutexError::NONE;
}

int RobustMutex::consistent() {
  if ((futex.load(cpp::MemoryOrder::RELAXED) & FUTEX_TID_MASK) !=
          uint32_t(internal::gettid()) ||
      !inconsistent)
    return EINVAL;
  inconsistent = 0;
  return 0;
}

int RobustMutex::destroy() {
  return (futex.load(cpp::MemoryOrder::RELAXED) & FUTEX_TID_MASK) ? EBUSY : 0;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
