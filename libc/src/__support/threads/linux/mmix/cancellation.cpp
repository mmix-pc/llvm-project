//===-- MMIX Linux deferred cancellation ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cancellation.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "lifecycle.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <asm/rstack.h>
#include <asm/unistd.h>
#include <pthread.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
class SignalMask {
  uint64_t saved;
  bool restore = true;

public:
  SignalMask() {
    uint64_t full = UINT64_MAX;
    if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&full),
                     reinterpret_cast<long>(&saved), sizeof(saved)))
      __builtin_trap();
  }
  ~SignalMask() {
    if (!restore)
      return;
    if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&saved), 0, sizeof(saved)))
      __builtin_trap();
  }
  uint64_t transfer() {
    restore = false;
    return saved;
  }
};
} // namespace

int cancel_thread(ThreadAttributes *handle) {
  SignalMask mask;
  // The current activation retains its own identity, without a sender lease.
  if (current_control && handle == &current_control->attributes) {
    ThreadRegistryLock lock(thread_registry);
    return lock.request_cancellation(*current_control, true);
  }
  ThreadControl *control;
  int error = ESRCH;
  {
    ThreadRegistryLock lock(thread_registry);
    control = lock.pin(handle);
    if (!control)
      return error;
    if (lock.acquire_lease(*control)) {
      error = lock.request_cancellation(*control);
      if (!lock.release_lease(*control))
        __builtin_trap();
    }
    if (!lock.unpin(*control))
      __builtin_trap();
  }
  // FIXME: Connect leased private notification when audited blocking windows
  // are available; sending must precede lease release outside the lock. This
  // preparation exports only explicit testcancel polling.
  return error;
}

int set_cancel_state(int state, int *old_state) {
  if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
    return EINVAL;
  if (!current_control)
    return ENOTSUP;
  SignalMask mask;
  bool enabled =
      current_control->cancellation.set_enabled(state == PTHREAD_CANCEL_ENABLE);
  if (old_state)
    *old_state = enabled ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE;
  return 0;
}

int set_cancel_type(int type, int *old_type) {
  if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
    return EINVAL;
  if (type == PTHREAD_CANCEL_ASYNCHRONOUS || !current_control)
    return ENOTSUP;
  if (old_type)
    *old_type = PTHREAD_CANCEL_DEFERRED;
  return 0;
}

[[gnu::noinline, clang::disable_tail_calls]] void test_cancel() {
  auto *control = current_control;
  if (!control || !control->cancellation.ready())
    return;
  bool claimed = false;
  uint64_t callback_mask = 0;
  {
    SignalMask mask;
    mmix_rstack_query query{};
    if (syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)))
      __builtin_trap();
    // Application-handler activations cannot borrow an ordinary unwind root.
    if (!control->unwind_root.active ||
        query.chain_id != control->unwind_root.chain)
      return;
    {
      ThreadRegistryLock lock(thread_registry);
      claimed = lock.begin_cleanup(*control, true);
    }
    if (claimed)
      callback_mask = mask.transfer();
  }
  if (claimed)
    unwind_after_cleanup_claim(PTHREAD_CANCELED, callback_mask);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
