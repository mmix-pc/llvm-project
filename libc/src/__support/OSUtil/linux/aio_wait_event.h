//===-- Private Linux AIO wait event ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_WAIT_EVENT_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_WAIT_EVENT_H

#include "hdr/errno_macros.h"
#include "hdr/types/struct_timespec.h"
#include "src/__support/CPP/type_traits/is_trivially_destructible.h"
#include "src/__support/threads/linux/futex_utils.h"

namespace LIBC_NAMESPACE_DECL {

// Private one-waiter latch, not a public AIO or signal-safety guarantee.
// Publish under AioGate; wait outside it. The external owner must detach all
// observers under that same gate before this storage dies, including on error,
// cancellation or an otherwise legal longjmp. There is no destructor cleanup,
// allocation or reset. Keep the event at a stable address for its lifetime.
class AioWaitEvent {
  // MMIX subword atomics require ownership of the entire containing octa.
  struct alignas(8) Storage {
    Futex word{0};
  } storage;
  static_assert(sizeof(Storage) == 8 && alignof(Storage) == 8);

public:
  // Raw futex syscall result (zero or negative errno), without touching errno.
  // The syscall number is FUTEX_SYSCALL_ID and uaddr2 is always null. timeout
  // has that syscall's kernel ABI, not necessarily the native timespec ABI.
  // This synchronous internal hook may supply a future cancellation boundary;
  // it must not retain the arguments or reset the word. No public cancel here.
  using WaitSyscall = int (*)(void *context, Futex *word, int operation,
                              FutexWordType expected, const void *timeout,
                              unsigned bitset);
  // Internal nonblocking wake-one only, called under the gate after the store.
  // Return the same checked result as Futex::notify_one(), preserving errno.
  using WakeOne = ErrorOr<int> (*)(void *context, Futex &word);

private:
  LIBC_INLINE static int linux_wait(void *, Futex *word, int operation,
                                    FutexWordType expected, const void *timeout,
                                    unsigned bitset) {
    return syscall_impl<int>(FUTEX_SYSCALL_ID, word, operation, expected,
                             timeout, nullptr, bitset);
  }
  LIBC_INLINE static ErrorOr<int> linux_wake(void *, Futex &word) {
    return word.notify_one();
  }

public:
  LIBC_INLINE constexpr AioWaitEvent() = default;
  AioWaitEvent(const AioWaitEvent &) = delete;
  AioWaitEvent &operator=(const AioWaitEvent &) = delete;

  LIBC_INLINE bool ready() {
    return storage.word.load(cpp::MemoryOrder::ACQUIRE) != 0;
  }

  LIBC_INLINE void publish(WakeOne wake_one = linux_wake,
                           void *context = nullptr) {
    storage.word.store(1, cpp::MemoryOrder::RELEASE);
    if (!wake_one(context, storage.word)) {
      // Returning could strand the sole waiter despite the latched value.
      (void)syscall_impl<long>(SYS_exit_group, 127);
      __builtin_trap();
    }
  }

  // Null means unbounded; otherwise the deadline is absolute CLOCK_MONOTONIC.
  // Returns zero for observed completion, otherwise positive errno. Completion
  // observed at a check wins, including over an invalid or expired deadline.
  // A return does not detach observers or end the publisher's use of storage.
  LIBC_INLINE int wait(const timespec *deadline = nullptr,
                       WaitSyscall wait_syscall = linux_wait,
                       void *context = nullptr) {
    if (ready())
      return 0;

    // Copy once so retries never extend the deadline. Match Futex's time64
    // selection, converting fields even if native timespec has another layout.
#if defined(SYS_futex_time64)
    __kernel_timespec timeout{};
#else
    timespec timeout{};
#endif
    const void *timeout_ptr = nullptr;
    if (deadline) {
      timespec absolute = *deadline;
      if (absolute.tv_nsec < 0 || absolute.tv_nsec >= 1000000000)
        return ready() ? 0 : EINVAL;
      if (absolute.tv_sec < 0)
        return ready() ? 0 : EAGAIN;
      timeout.tv_sec = absolute.tv_sec;
      timeout.tv_nsec = absolute.tv_nsec;
      timeout_ptr = &timeout;
    }

    for (;;) {
      if (ready())
        return 0;
      int result =
          wait_syscall(context, &storage.word, FUTEX_WAIT_BITSET_PRIVATE, 0,
                       timeout_ptr, FUTEX_BITSET_MATCH_ANY);
      if (ready())
        return 0;
      if (result == 0 || result == -EAGAIN)
        continue;
      if (result == -ETIMEDOUT)
        return EAGAIN;
      return result < 0 ? -result : EIO;
    }
  }
};

static_assert(cpp::is_trivially_destructible_v<AioWaitEvent>);
static_assert(sizeof(AioWaitEvent) == 8 && alignof(AioWaitEvent) == 8);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_WAIT_EVENT_H
