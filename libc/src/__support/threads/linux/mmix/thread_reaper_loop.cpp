//===-- MMIX Linux deferred thread reclamation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_reaper.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <linux/futex.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static timespec retry_deadline() {
  timespec ts;
  if (syscall_impl(SYS_clock_gettime, CLOCK_MONOTONIC,
                   reinterpret_cast<long>(&ts)) != 0 ||
      ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000)
    __builtin_trap();
  ts.tv_nsec += 10000000;
  if (ts.tv_nsec >= 1000000000) {
    if (ts.tv_sec == cpp::numeric_limits<time_t>::max())
      __builtin_trap();
    ++ts.tv_sec;
    ts.tv_nsec -= 1000000000;
  }
  return ts;
}

bool reap_detached_pass() {
  size_t count;
  {
    ThreadRegistryLock lock(thread_registry);
    count = lock.queued_reclaims();
  }
  bool progress = false;
  while (count--) {
    ThreadControl *control;
    ThreadResources resources;
    bool reaped = false;
    uint32_t clear;
    {
      ThreadRegistryLock lock(thread_registry);
      control = lock.take_reap_candidate();
      if (!control)
        __builtin_trap();
      clear = control->clear_tid.value.load(cpp::MemoryOrder::ACQUIRE);
      if (!clear) {
        if (lock.owner(*control) == ThreadOwner::Detached &&
            !lock.begin_reaping(*control))
          __builtin_trap();
        resources = thread_resources(*control);
        reaped = lock.finish_reaping(*control);
      }
      if (!reaped && !lock.requeue_candidate(*control))
        __builtin_trap();
    }
    if (reaped) {
      if (release_thread_resources(resources))
        __builtin_trap();
      progress = true;
      // No control access after release, including notification or diagnostics.
    } else if (clear) {
      // The queue still owns the lifecycle pin. A single bounded shared wait
      // cannot strand later records behind a delayed kernel exit.
      timespec deadline = retry_deadline();
      long ret = syscall_impl(SYS_futex,
          reinterpret_cast<long>(&control->clear_tid.value.val),
          FUTEX_WAIT_BITSET, clear, reinterpret_cast<long>(&deadline), 0,
          FUTEX_BITSET_MATCH_ANY);
      if (ret != 0 && ret != -EINTR && ret != -EAGAIN && ret != -ETIMEDOUT)
        __builtin_trap();
    }
  }
  return progress;
}

extern "C" void __llvm_libc_mmix_reaper_loop() {
  for (;;) {
    uint32_t sequence;
    {
      ThreadRegistryLock lock(thread_registry);
      sequence = lock.sequence();
    }
    if (reap_detached_pass())
      continue;
    // Capture before scanning so publication during/after a pass is not lost.
    // Rotation emits no notification; empty/pinned-only queues sleep here.
    auto waited = thread_registry.wait(
        sequence, *Futex::Timeout::from_timespec(retry_deadline(), false));
    if (!waited && waited.error() != ETIMEDOUT)
      __builtin_trap();
  }
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
