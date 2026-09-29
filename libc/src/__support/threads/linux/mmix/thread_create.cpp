//===-- MMIX Linux internal thread creation -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_create.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/time_macros.h"
#include "hdr/types/struct_timespec.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "thread_entry.h"
#include <linux/futex.h>
#include <linux/sched.h>
#include <sys/syscall.h>

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM || \
    LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL
#error "MMIX Linux thread creation requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static void restore_mask(uint64_t mask) {
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&mask), 0, sizeof(mask)) != 0)
    __builtin_trap();
}

static void cancel_reservation() {
  ThreadRegistryLock lock(thread_registry);
  if (!lock.cancel_reservation())
    __builtin_trap();
}

static void wake_decision(ThreadControl &control) {
  if (!control.creator_decision.value.notify_all())
    __builtin_trap();
}

static long await_startup(ThreadControl &control) {
  for (;;) {
    ChildStatus status = control.acquire_child();
    if (status == ChildStatus::Failed) {
      if (control.child_error <= 0 || control.child_error > 4095)
        __builtin_trap();
      return -control.child_error;
    }
    // The kernel can retire a clone-published child before its first user
    // instruction. A timeout is only an opportunity to recheck this word.
    // Preserve a FAILED payload even if the kernel has also cleared TID.
    if (control.clear_tid.value.load(cpp::MemoryOrder::ACQUIRE) == 0)
      return -EAGAIN;
    if (status == ChildStatus::Ready)
      return 0;
    if (status != ChildStatus::Starting)
      __builtin_trap();

    timespec deadline;
    if (syscall_impl(SYS_clock_gettime, CLOCK_MONOTONIC,
                     reinterpret_cast<long>(&deadline)) != 0 ||
        deadline.tv_sec < 0 || deadline.tv_nsec < 0 ||
        deadline.tv_nsec >= 1000000000)
      __builtin_trap();
    // Poll the independent terminal predicate at bounded intervals, without
    // imposing a scheduling deadline on a still-live child.
    deadline.tv_nsec += 10000000;
    if (deadline.tv_nsec >= 1000000000) {
      if (deadline.tv_sec == cpp::numeric_limits<time_t>::max())
        __builtin_trap();
      ++deadline.tv_sec;
      deadline.tv_nsec -= 1000000000;
    }
    long ret = syscall_impl(
        SYS_futex, reinterpret_cast<long>(&control.child_status.value.val),
        FUTEX_WAIT_BITSET_PRIVATE, static_cast<uint32_t>(ChildStatus::Starting),
        reinterpret_cast<long>(&deadline), 0, FUTEX_BITSET_MATCH_ANY);
    if (ret < 0 && ret != -EINTR && ret != -EAGAIN && ret != -ETIMEDOUT)
      __builtin_trap();
  }
}

ThreadCreationResult create_thread(const ThreadPreparation &request,
                                   ThreadAttributes *&output) {
  ThreadPreparation input = request;
  uint64_t blocked = UINT64_MAX, saved_mask = 0;
  long ret = syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                          reinterpret_cast<long>(&blocked),
                          reinterpret_cast<long>(&saved_mask), sizeof(blocked));
  if (ret < 0)
    return {ret};
  if (ret != 0)
    __builtin_trap();
  bool reserved;
  {
    ThreadRegistryLock lock(thread_registry);
    reserved = lock.reserve();
  }
  if (!reserved) {
    restore_mask(saved_mask);
    return {-EAGAIN};
  }
  input.saved_signal_mask = saved_mask;
  ThreadControl *control = nullptr;
  ThreadPreparationResult prepared = prepare_thread(input, control);
  if (prepared.error != ThreadPrepareError::None) {
    cancel_reservation();
    restore_mask(saved_mask);
    long error = prepared.syscall_error ? prepared.syscall_error
                 : prepared.error == ThreadPrepareError::Overflow ? EOVERFLOW
                                                                  : EINVAL;
    return {-error, prepared};
  }

  constexpr unsigned long flags =
      CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
      CLONE_SYSVSEM | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_SETTLS;
  ret = __llvm_libc_mmix_clone_thread(
      flags, control->stack_top, &control->parent_tid.value.val,
      &control->clear_tid.value.val, control->tls.tp);
  if (ret < 0) {
    ThreadResources resources = thread_resources(*control);
    prepared.rollback_error = release_thread_resources(resources);
    prepared.retained = resources;
    cancel_reservation();
    restore_mask(saved_mask);
    return {ret, prepared};
  }
  if (ret == 0 || ret > cpp::numeric_limits<int>::max() ||
      control->parent_tid.value.load(cpp::MemoryOrder::ACQUIRE) != ret)
    __builtin_trap();

  long error = await_startup(*control);
  if (error) {
    {
      ThreadRegistryLock lock(thread_registry);
      if (!lock.adopt_abort(*control))
        __builtin_trap();
    }
    control->publish_decision(CreatorDecision::Abort);
    wake_decision(*control);
    // The delegate consumes the creator pin. Copy all caller state first;
    // no control access is permitted after it returns.
    __llvm_libc_mmix_reclaim_failed_thread(control);
    cancel_reservation();
    restore_mask(saved_mask);
    return {error};
  }
  if (control->attributes.tid != ret)
    __builtin_trap();
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.insert(*control, input.detached) || !lock.start(*control))
      __builtin_trap();
    output = &control->attributes;
  }
  control->publish_decision(CreatorDecision::Go);
  wake_decision(*control);
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.drop_creator(*control))
      __builtin_trap();
  }
  restore_mask(saved_mask);
  return {};
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
