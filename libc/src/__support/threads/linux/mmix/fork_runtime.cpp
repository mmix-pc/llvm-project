//===-- MMIX Linux fork runtime snapshot ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "fork_runtime.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "main_thread.h"
#include "once.h"
#include "src/__support/CPP/new.h"
#include "src/__support/File/file.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/stdio/stderr.h"
#include "src/stdio/stdin.h"
#include "src/stdio/stdout.h"
#include "src/stdlib/environ_internal.h"
#include "src/stdlib/linux/mmix/allocator.h"
#include "thread_reaper.h"
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "src/dlfcn/linux/mmix/loading.h"
#endif
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
struct alignas(8) Serializer {
  RawMutex mutex;
};
LIBC_CONSTINIT Serializer serializer;
LIBC_CONSTINIT LIBC_THREAD_LOCAL bool active_fork = false;
} // namespace

ForkOperation::ForkOperation() {
  if (active_fork) {
    error_code = EDEADLK;
    return;
  }
  caller = current_control;
  if (!caller || caller->fork_child) {
    error_code = ENOTSUP;
    return;
  }
  // Do not leave the serializer owned if async delivery interrupts admission.
  uint64_t full = UINT64_MAX;
  long result = syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &full,
                                   &saved_mask, sizeof(saved_mask));
  if (result < 0) {
    error_code = int(-result);
    return;
  }
  if (result)
    __builtin_trap();
  masked = true;
  if (!serializer.mutex.try_lock()) {
    error_code = EAGAIN;
    return;
  }
  serial = active_fork = true;
  if (caller->cancellation_transfer.load() ||
      caller->cancellation_point.load() ||
      caller->cancellation_suppression.load() || !once_can_fork()) {
    error_code = EAGAIN;
    return;
  }
  caller->cancellation_suppression.fetch_add(1);
  suppressed = true;
  // Finish shared environment bootstrap before copying its initializing state.
  // Environment mutation still requires application synchronization.
  (void)internal::EnvironmentManager::get_instance();
}

int ForkOperation::prepare_runtime() {
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (int error = prepare_loader_fork(&resume_loader))
    return error;
  loader_locked = true;
#endif
  if (caller->priority_protection.guard.lock() != MutexError::NONE)
    __builtin_trap();
  priority = true;
  File::lock_list();
  standard[0] = reinterpret_cast<File *>(LIBC_NAMESPACE::stdin);
  standard[1] = reinterpret_cast<File *>(LIBC_NAMESPACE::stdout);
  standard[2] = reinterpret_cast<File *>(LIBC_NAMESPACE::stderr);
  for (unsigned i = 0; i < 3; ++i) {
    bool duplicate = false;
    for (unsigned j = 0; j < i; ++j)
      duplicate |= standard[j] == standard[i];
    if (standard[i] && !duplicate) {
      standard[i]->lock();
      standard_locked[i] = true;
    }
  }
  for (auto *file = File::get_first_file(); file; file = file->get_next())
    if (file != standard[0] && file != standard[1] && file != standard[2])
      file->lock();
  streams = true;
  allocator_fork_prepare();
  allocator = true;
  return 0;
}

int ForkOperation::freeze_registry() {
  bool single_thread = false;
  registry = thread_registry.freeze_for_fork(*caller, 1, &single_thread);
#ifdef LIBC_MMIX_SHARED_STARTUP
  resume_loader &= single_thread;
#endif
  return registry ? 0 : EAGAIN;
}

void ForkOperation::restore_runtime(bool child) {
  if (registry) {
    if (child)
      thread_registry.restore_fork_child(*caller);
    else
      thread_registry.release_after_fork();
    registry = false;
  }
  if (allocator) {
    allocator_fork_release();
    allocator = false;
  }
  if (streams) {
    // Release recursive stream ownership using the snapshot's cached TID.
    // Signals are blocked; publish the child's real identity only afterwards.
    for (auto *file = File::get_first_file(); file; file = file->get_next())
      if (file != standard[0] && file != standard[1] && file != standard[2])
        file->unlock();
    for (unsigned i = 3; i; --i)
      if (standard_locked[i - 1])
        standard[i - 1]->unlock();
    File::unlock_list();
    streams = false;
  }
  if (priority) {
    if (child) {
      new (&caller->priority_protection.guard)
          RobustMutex{false, false, false, true, false};
    } else if (caller->priority_protection.guard.unlock() != MutexError::NONE)
      __builtin_trap();
    priority = false;
  }
  if (child) {
    long tid = syscall_impl<long>(SYS_gettid);
    if (tid <= 0 || tid > cpp::numeric_limits<int>::max())
      internal::exit(127);
    caller->attributes.tid = int(tid);
    caller->unwind_root.active = false;
    caller->cancellation.restore_after_fork();
    caller->cancellation_identity = caller->cancellation_generation = 0;
    caller->cancellation_point.store(nullptr);
    caller->cancellation_transfer.store(0);
    restore_reaper_after_fork();
    once_after_fork();
    if (!internal::adopt_fork_survivor())
      internal::exit(127);
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (loader_locked) {
    finish_loader_fork(child, resume_loader);
    loader_locked = false;
  }
#endif
}

ForkOperation::~ForkOperation() {
  restore_runtime(false);
  if (suppressed && caller->cancellation_suppression.fetch_sub(1) != 1)
    __builtin_trap();
  if (serial) {
    active_fork = false;
    serializer.mutex.unlock();
  }
  if (masked && syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask,
                                   0, sizeof(saved_mask)))
    __builtin_trap();
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
