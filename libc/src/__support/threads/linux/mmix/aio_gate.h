//===-- MMIX AIO state gate ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_GATE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_GATE_H

#include "credentials.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/threads/raw_mutex.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Every service-state access, including the worker and retirement, uses this
// gate. No allocation, loader calls, callbacks, descriptor operations or waits
// for I/O may run while held. The private credential handler never uses it.
// Masking the cancellation signal protects this short region without changing
// the application's cancellation state or introducing a cancellation point.
// This does not authorize handler-side submission or handler cancellation.
// Fork must quiesce the service, or quarantine the copied gate without locking
// it; a child must never use a potentially locked inherited service instance.
class alignas(8) AioGate {
  // Subword atomics on MMIX require the whole containing octa to be owned.
  struct alignas(8) LockStorage {
    RawMutex value;
  } lock;

public:
  constexpr AioGate() = default;
  AioGate(const AioGate &) = delete;
  AioGate &operator=(const AioGate &) = delete;

  // Fork has already blocked application/cancellation signals. Never wait
  // while holding other runtime snapshot locks; thaw before releasing them.
  bool try_freeze() { return lock.value.try_lock(); }
  void thaw() {
    if (!lock.value.unlock())
      __builtin_trap();
  }

  class Guard {
    AioGate &gate;
    uint64_t saved_mask = 0;
    int error_code = 0;
    bool held = false;

  public:
    explicit Guard(AioGate &owner) : gate(owner) {
      uint64_t blocked = UINT64_MAX & ~CREDENTIAL_MASK;
      long result = syscall_impl<long>(SYS_rt_sigprocmask, SIG_BLOCK, &blocked,
                                       &saved_mask, sizeof(saved_mask));
      if (result < 0) {
        error_code = int(-result);
        return;
      }
      if (!gate.lock.value.lock())
        __builtin_trap();
      held = true;
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
    ~Guard() {
      if (!held)
        return;
      if (!gate.lock.value.unlock())
        __builtin_trap();
      // A pending application handler may immediately reenter this gate.
      if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask,
                             nullptr, sizeof(saved_mask)) != 0)
        __builtin_trap();
    }
    int error() const { return error_code; }
    explicit operator bool() const { return held; }
  };
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif
