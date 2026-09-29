//===-- MMIX Linux terminal thread reclamation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_reclaim.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "thread_create.h"
#include <linux/futex.h>
#include <sys/syscall.h>

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM ||                           \
    LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL
#error "MMIX Linux thread reclamation requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static timespec deadline() {
  timespec ts;
  if (syscall_impl(SYS_clock_gettime, CLOCK_MONOTONIC,
                   reinterpret_cast<long>(&ts)) != 0 ||
      ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000 ||
      ts.tv_sec == cpp::numeric_limits<time_t>::max())
    __builtin_trap();
  ++ts.tv_sec;
  return ts;
}

void await_thread_clear(ThreadControl &control) {
  for (;;) {
    uint32_t value = control.clear_tid.value.load(cpp::MemoryOrder::ACQUIRE);
    if (!value)
      return;
    timespec ts = deadline();
    // Linux clear_child_tid uses the shared key, even for CLONE_THREAD.
    long ret = syscall_impl(
        SYS_futex, reinterpret_cast<long>(&control.clear_tid.value.val),
        FUTEX_WAIT_BITSET, value, reinterpret_cast<long>(&ts), 0,
        FUTEX_BITSET_MATCH_ANY);
    if (ret != 0 && ret != -EINTR && ret != -EAGAIN && ret != -ETIMEDOUT)
      __builtin_trap();
  }
}

long reclaim_thread(ThreadControl &control, ThreadOwner owner,
                    ThreadResources &retained) {
  if (owner != ThreadOwner::AbortOwner && owner != ThreadOwner::JoinOwner &&
      owner != ThreadOwner::Detached)
    __builtin_trap();
  {
    ThreadRegistryLock lock(thread_registry);
    if (lock.owner(control) != owner ||
        control.retains_resources_until_process_exit())
      __builtin_trap();
  }
  await_thread_clear(control);
  {
    ThreadRegistryLock lock(thread_registry);
    if (lock.owner(control) != owner || !lock.begin_reaping(control))
      __builtin_trap();
    if ((owner == ThreadOwner::AbortOwner && !lock.drop_creator(control)) ||
        (owner == ThreadOwner::JoinOwner && !lock.unpin(control)))
      __builtin_trap();
    retained = thread_resources(control);
  }
  for (;;) {
    bool drained;
    uint32_t sequence;
    {
      ThreadRegistryLock lock(thread_registry);
      drained = lock.finish_reaping(control);
      sequence = lock.sequence();
    }
    if (drained)
      break;
    auto waited = thread_registry.wait(
        sequence, *Futex::Timeout::from_timespec(deadline(), false));
    if (!waited && waited.error() != ETIMEDOUT)
      __builtin_trap();
  }
  // Only owner-local descriptors survive this call. Notification after the
  // final pin release uses the process-lifetime event, never the control word.
  return release_thread_resources(retained);
}

extern "C" void __llvm_libc_mmix_reclaim_failed_thread(ThreadControl *control) {
  if (!control)
    __builtin_trap();
  ThreadResources retained;
  if (reclaim_thread(*control, ThreadOwner::AbortOwner, retained))
    __builtin_trap();
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
