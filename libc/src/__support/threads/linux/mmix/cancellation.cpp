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
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "cancellation_point.h"
#endif

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
    int error;
    {
      ThreadRegistryLock lock(thread_registry);
      error = lock.request_cancellation(*current_control, true);
    }
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
    if (!error && current_control->cancellation.enabled() &&
        (current_control->cancellation.asynchronous() ||
         current_control->cancellation_point.load(cpp::MemoryOrder::ACQUIRE))) {
      long pid = syscall_impl(SYS_getpid);
      long result = pid < 0 ? pid : syscall_impl(
          SYS_tgkill, pid, current_control->attributes.tid, CANCEL_SIGNAL);
      if (result < 0)
        error = static_cast<int>(-result);
    }
#endif
    return error;
  }
  ThreadControl *control;
  int error = ESRCH;
  int tid = 0;
  bool notify = false;
  {
    ThreadRegistryLock lock(thread_registry);
    control = lock.pin(handle);
    if (!control)
      return error;
    if (lock.acquire_lease(*control)) {
      error = lock.request_cancellation(*control);
      tid = control->attributes.tid;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
      notify = !error && control->cancellation.enabled();
#endif
    } else {
      if (!lock.unpin(*control))
        __builtin_trap();
      return error;
    }
  }
  if (notify) {
    long pid = syscall_impl(SYS_getpid);
    long result = pid < 0 ? pid : syscall_impl(SYS_tgkill, pid, tid, 32);
    if (result < 0)
      error = static_cast<int>(-result);
  }
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.release_lease(*control) || !lock.unpin(*control))
      __builtin_trap();
  }
  return error;
}

int set_cancel_state(int state, int *old_state) {
  if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
    return EINVAL;
  if (!current_control)
    return ENOTSUP;
#ifdef LIBC_MMIX_SHARED_STARTUP
  // Application callbacks cannot re-enable delivery inside a loader
  // transaction.
  if (state == PTHREAD_CANCEL_ENABLE && current_control->loader_scopes.load())
    return EINVAL;
#endif
  {
    SignalMask mask;
    bool enabled = current_control->cancellation.set_enabled(
        state == PTHREAD_CANCEL_ENABLE);
    if (old_state)
      *old_state = enabled ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  // An application handler can enable a pending request while its ordinary
  // caller is blocked. Queue the wake before returning to that caller's TRAP.
  if (state == PTHREAD_CANCEL_ENABLE && current_control->cancellation.ready() &&
      (current_control->cancellation.asynchronous() ||
       current_control->cancellation_point.load(cpp::MemoryOrder::ACQUIRE))) {
    long pid = syscall_impl(SYS_getpid);
    if (pid <= 0 || syscall_impl(SYS_tgkill, pid,
                                current_control->attributes.tid, CANCEL_SIGNAL))
      __builtin_trap();
  }
#endif
  }
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  if (state == PTHREAD_CANCEL_ENABLE &&
      current_control->cancellation.asynchronous())
    test_cancel();
#endif
  return 0;
}

int set_cancel_type(int type, int *old_type) {
  if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
    return EINVAL;
  if (!current_control)
    return ENOTSUP;
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  {
    SignalMask mask;
    bool previous = current_control->cancellation.set_async(
        type == PTHREAD_CANCEL_ASYNCHRONOUS);
    if (old_type)
      *old_type =
          previous ? PTHREAD_CANCEL_ASYNCHRONOUS : PTHREAD_CANCEL_DEFERRED;
  }
  if (type == PTHREAD_CANCEL_ASYNCHRONOUS)
    test_cancel();
#else
  if (type == PTHREAD_CANCEL_ASYNCHRONOUS)
    return ENOTSUP;
  if (old_type)
    *old_type = PTHREAD_CANCEL_DEFERRED;
#endif
  return 0;
}

[[gnu::noinline, clang::disable_tail_calls]] void test_cancel() {
  auto *control = current_control;
  if (!control || !control->cancellation.ready())
    return;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  if (cancellation_private_block())
    return;
#endif
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
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  callback_mask |= CANCEL_MASK;
#endif
  if (claimed)
    unwind_after_cleanup_claim(PTHREAD_CANCELED, callback_mask);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
