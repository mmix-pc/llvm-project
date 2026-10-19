//===-- MMIX Linux fork runtime snapshot ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "fork_runtime.h"
#include "credentials.h"
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
// Optional providers must not be pulled into a static link merely by fork.
extern Mutex handler_list_mtx __attribute__((weak));
namespace mmix {
extern Mutex quick_exit_mutex __attribute__((weak));
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
  if (!caller || !caller->fork_recovery.allows_fork()) {
    error_code = ENOTSUP;
    return;
  }
  // Do not leave the serializer owned if async delivery interrupts admission.
  uint64_t full = UINT64_MAX & ~CREDENTIAL_MASK;
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
  // Helper TLS retirement can enter the loader and allocator. Complete it
  // before either subsystem is locked for the clone snapshot.
  helper_drained = retire_reaper_for_fork(*caller);
  if (!prepare_credentials_fork())
    return EAGAIN;
  credentials = true;
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (int error = prepare_loader_fork(&resume_loader))
    return error;
  loader_locked = true;
#endif
  if (caller->priority_protection.guard.lock() != MutexError::NONE)
    __builtin_trap();
  priority = true;
  // Generic static atexit can allocate while holding its list lock. Acquire
  // callback locks before the allocator; callback invocation holds neither.
  if (&handler_list_mtx) {
    handler_list_mtx.lock();
    exit_callbacks = true;
  }
  if (&quick_exit_mutex) {
    quick_exit_mutex.lock();
    quick_callbacks = true;
  }
  // A caller may already own a stream while another thread holds the list
  // lock waiting for that stream. Never wait in this inverse ownership order.
  if (File::list_lock.try_lock() != MutexError::NONE)
    return EAGAIN;
  streams = true;
  stream_stop = File::get_first_file();
  standard[0] = reinterpret_cast<File *>(LIBC_NAMESPACE::stdin);
  standard[1] = reinterpret_cast<File *>(LIBC_NAMESPACE::stdout);
  standard[2] = reinterpret_cast<File *>(LIBC_NAMESPACE::stderr);
  for (unsigned i = 0; i < 3; ++i) {
    bool duplicate = false;
    for (unsigned j = 0; j < i; ++j)
      duplicate |= standard[j] == standard[i];
    if (standard[i] && !duplicate) {
      if (!standard[i]->try_lock())
        return EAGAIN;
      standard_locked[i] = true;
    }
  }
  for (auto *file = File::get_first_file(); file; file = file->get_next()) {
    stream_stop = file;
    if (file != standard[0] && file != standard[1] && file != standard[2] &&
        !file->try_lock())
      return EAGAIN;
  }
  stream_stop = nullptr;
  allocator_fork_prepare();
  allocator = true;
  return 0;
}

int ForkOperation::freeze_registry() {
  bool single_thread = false;
  registry = thread_registry.freeze_for_fork(*caller, 1, &single_thread,
                                            helper_drained);
#ifdef LIBC_MMIX_SHARED_STARTUP
  resume_loader &= single_thread;
  child_recovery = internal::ForkRecovery::child(single_thread, resume_loader);
#else
  child_recovery = internal::ForkRecovery::child(single_thread, true);
#endif
  return registry ? 0 : EAGAIN;
}

void ForkOperation::restore_runtime(bool child) {
  if (registry) {
    if (child)
      thread_registry.restore_fork_child(*caller, child_recovery);
    else
      thread_registry.release_after_fork();
    registry = false;
  }
  if (allocator) {
    allocator_fork_release();
    allocator = false;
  }
  if (child) {
    long tid = syscall_impl<long>(SYS_gettid);
    if (tid <= 0 || tid > cpp::numeric_limits<int>::max())
      internal::exit(127);
    pid_t previous = caller->attributes.tid;
    for (unsigned i = 0; i < 3; ++i)
      if (standard_locked[i] &&
          !standard[i]->rebind_owner_after_fork(previous, pid_t(tid)))
        internal::exit(127);
    for (auto *file = File::get_first_file(); file; file = file->get_next())
      if (file != standard[0] && file != standard[1] && file != standard[2] &&
          !file->rebind_owner_after_fork(previous, pid_t(tid)))
        internal::exit(127);
    caller->attributes.tid = pid_t(tid);
    restore_credentials_after_fork(*caller);
  }
  if (streams) {
    // Drop only our snapshot acquisition, retaining application recursion.
    for (auto *file = File::get_first_file(); file && file != stream_stop;
         file = file->get_next())
      if (file != standard[0] && file != standard[1] && file != standard[2])
        file->unlock();
    for (unsigned i = 3; i; --i)
      if (standard_locked[i - 1])
        standard[i - 1]->unlock();
    File::unlock_list();
    streams = false;
  }
  if (quick_callbacks) {
    quick_exit_mutex.unlock();
    quick_callbacks = false;
  }
  if (exit_callbacks) {
    handler_list_mtx.unlock();
    exit_callbacks = false;
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
    if (!child_recovery.allows_loader())
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
    finish_loader_fork(child, child_recovery.allows_loader());
    loader_locked = false;
  }
#endif
#ifdef LIBC_MMIX_UNWIND_ROOTS
  // Metadata queries are safe only after the quiescent loader has resumed.
  if (child && child_recovery.allows_loader() && !restore_fork_unwind(*caller))
    internal::exit(127);
#endif
  if (credentials) {
    finish_credentials_fork();
    credentials = false;
  }
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
