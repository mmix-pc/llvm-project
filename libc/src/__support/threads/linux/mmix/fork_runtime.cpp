//===-- MMIX Linux fork runtime snapshot ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "fork_runtime.h"
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "cancellation_point.h"
#endif
#include "credentials.h"
#include "fork_transport.h"
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
#include "thread_finish.h"
#include "thread_reaper.h"
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "src/dlfcn/linux/mmix/loading.h"
#endif
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
// Optional providers must not be pulled into a static link merely by fork.
extern Mutex handler_list_mtx __attribute__((weak));
namespace internal {
bool try_lock_domain_bindings_for_fork() __attribute__((weak));
void unlock_domain_bindings_after_fork() __attribute__((weak));
bool try_lock_message_results_for_fork() __attribute__((weak));
void unlock_message_results_after_fork() __attribute__((weak));
bool try_lock_random48_for_fork() __attribute__((weak));
void unlock_random48_after_fork() __attribute__((weak));
bool try_lock_random_for_fork() __attribute__((weak));
void unlock_random_after_fork() __attribute__((weak));
pid_t normal_exit_fork_owner(bool *) __attribute__((weak));
bool rebind_normal_exit_fork_owner(pid_t) __attribute__((weak));
pid_t last_thread_exit_fork_owner() __attribute__((weak));
bool rebind_last_thread_exit_fork_owner(pid_t) __attribute__((weak));
} // namespace internal
namespace mmix {
int prepare_notification_fork(bool, bool *) __attribute__((weak));
void finish_notification_fork(bool) __attribute__((weak));
void close_notification_admission() __attribute__((weak));
namespace {
struct alignas(8) Serializer {
  RawMutex mutex;
};
LIBC_CONSTINIT Serializer serializer;
LIBC_CONSTINIT LIBC_THREAD_LOCAL bool active_fork = false;
} // namespace

ForkOperation::ForkOperation(bool minimal) : minimal(minimal) {
  if (active_fork) {
    error_code = EDEADLK;
    return;
  }
  caller = current_control;
  if (!caller || !caller->fork_recovery.allows_fork()) {
    error_code = ENOTSUP;
    return;
  }
  if (minimal) {
#ifdef LIBC_MMIX_UNWIND_ROOTS
    // Reject interrupted execution before acquiring runtime locks. The census
    // includes unreaped helpers, not merely the caller's creation history.
    if (!ordinary_fork_context(*caller) ||
        !thread_registry.quiescent_fork_caller(*caller) ||
        caller->cancellation.terminal()) {
      error_code = EAGAIN;
      return;
    }
#else
    error_code = ENOTSUP;
    return;
#endif
#ifdef LIBC_MMIX_SHARED_STARTUP
    if (caller->loader_scopes.load() || caller->loader_callbacks.load()) {
      error_code = EAGAIN;
      return;
    }
#endif
  }
  // Do not leave the serializer owned if async delivery interrupts admission.
  uint64_t full = minimal ? UINT64_MAX : UINT64_MAX & ~CREDENTIAL_MASK;
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
      caller->cancellation_suppression.load()) {
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
#ifdef LIBC_MMIX_UNWIND_ROOTS
  bool during = false;
  if (!minimal && internal::last_thread_exit_fork_owner &&
      internal::rebind_last_thread_exit_fork_owner &&
      internal::last_thread_exit_fork_owner() == caller->attributes.tid &&
      last_thread_fork_context(*caller)) {
    continuation = ForkContinuation::LastThreadAfter;
    continuation_root = caller->unwind_root;
  } else if (!minimal &&
             initial_thread_during_fork_owner() == caller->attributes.tid &&
             terminal_thread_fork_context(*caller)) {
    continuation = ForkContinuation::InitialThreadDuring;
    continuation_root = caller->unwind_root;
  } else if (!minimal && internal::normal_exit_fork_owner &&
      internal::rebind_normal_exit_fork_owner &&
      internal::normal_exit_fork_owner(&during) == caller->attributes.tid &&
      ordinary_fork_context(*caller)) {
    continuation = during ? ForkContinuation::NormalDuring
                          : ForkContinuation::NormalAfter;
  }
#endif
  const bool cleanup_continuation = continuation != ForkContinuation::Ordinary;
  const bool terminal = continuation == ForkContinuation::LastThreadAfter ||
                        continuation == ForkContinuation::InitialThreadDuring;
  // Unsupported terminal/DURING/forced contexts must not attempt helper
  // retirement or enter ordinary child restoration as a fallback.
  if (caller->cancellation.terminal() && !terminal)
    return EAGAIN;
  if (terminal &&
      !reaper_absent_for_exit_fork(*caller, continuation))
    return EAGAIN;
  // Helper TLS retirement can enter the loader and allocator. Complete it
  // before either subsystem is locked for the clone snapshot.
  bool notification_drained = true;
  if (prepare_notification_fork) {
    if (int error = prepare_notification_fork(minimal, &notification_drained))
      return error;
    notifications = true;
  }
  if (!minimal)
    // A terminal continuation may witness absence, never retire a live helper.
    helper_drained =
        (cleanup_continuation
             ? reaper_absent_for_exit_fork(*caller, continuation)
             : retire_reaper_for_fork(*caller)) &&
        notification_drained;
  else
    // The minimal precheck already excluded all helpers and incomplete reaps.
    // Repeat the census at freeze; old creation history is not a live owner.
    helper_drained = notification_drained;
  if (!prepare_credentials_fork())
    return EAGAIN;
  credentials = true;
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (int error = prepare_loader_fork(&resume_loader))
    return error;
  loader_locked = true;
  if (minimal && !resume_loader)
    return EAGAIN;
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
  // Quick callbacks are immutable once atomically published and their nodes
  // and image pins survive until process exit; they need no snapshot lock.
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
  // Translation storage can allocate while locked. Freeze it before the
  // allocator, without waiting for a peer or discarding retained strings.
  if (internal::try_lock_domain_bindings_for_fork) {
    if (!internal::try_lock_domain_bindings_for_fork())
      return EAGAIN;
    message_bindings = true;
  }
  if (internal::try_lock_message_results_for_fork) {
    if (!internal::try_lock_message_results_for_fork())
      return EAGAIN;
    message_results = true;
  }
  if (internal::try_lock_random48_for_fork) {
    if (!internal::try_lock_random48_for_fork())
      return EAGAIN;
    random48_locked = true;
  }
  if (internal::try_lock_random_for_fork) {
    if (!internal::try_lock_random_for_fork())
      return EAGAIN;
    random_locked = true;
  }
  allocator_fork_prepare();
  allocator = true;
  return 0;
}

int ForkOperation::freeze_registry() {
  bool single_thread = false;
  registry = thread_registry.freeze_for_fork(*caller, 1, &single_thread,
                                             helper_drained, continuation);
  // Recheck the exact owner/root selected before locks. These hooks inspect
  // only owner-local identity and phase, never query locked loader metadata.
#ifdef LIBC_MMIX_UNWIND_ROOTS
  if (registry && continuation == ForkContinuation::InitialThreadDuring &&
      (initial_thread_during_fork_owner() != caller->attributes.tid ||
       !continuation_root.matches(caller->unwind_root))) {
    thread_registry.release_after_fork();
    registry = false;
    return EAGAIN;
  }
#endif
  if (registry && continuation == ForkContinuation::LastThreadAfter &&
      (internal::last_thread_exit_fork_owner() != caller->attributes.tid ||
       !continuation_root.matches(caller->unwind_root))) {
    thread_registry.release_after_fork();
    registry = false;
    return EAGAIN;
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  resume_loader &= single_thread;
  child_recovery = internal::ForkRecovery::child(single_thread, resume_loader);
#else
  child_recovery = internal::ForkRecovery::child(single_thread, true);
#endif
  return registry &&
                 (!(minimal || continuation != ForkContinuation::Ordinary) ||
                  child_recovery.allows_fork())
             ? 0
             : EAGAIN;
}

long ForkOperation::create_child(bool wait_for_exec) {
  if (!registry)
    __builtin_trap();
  ForkResult result = fork_process(wait_for_exec);
  previous_tid = result.previous_tid;
  return result.value;
}

void ForkOperation::restore_runtime(bool child) {
  const bool initial_during =
      continuation == ForkContinuation::InitialThreadDuring;
  const bool process_cleanup =
      continuation != ForkContinuation::Ordinary && !initial_during;
  const bool terminal =
      continuation == ForkContinuation::LastThreadAfter || initial_during;
  // Capture provenance before child registry restoration promotes the worker.
  const bool worker_cleanup =
      process_cleanup && !caller->retains_resources_until_process_exit();
  if (registry) {
    if (child)
      thread_registry.restore_fork_child(*caller, child_recovery,
                                         continuation);
    else
      thread_registry.release_after_fork();
    registry = false;
  }
  if (allocator) {
    allocator_fork_release();
    allocator = false;
  }
  if (random_locked) {
    internal::unlock_random_after_fork();
    random_locked = false;
  }
  if (random48_locked) {
    internal::unlock_random48_after_fork();
    random48_locked = false;
  }
  if (message_results) {
    internal::unlock_message_results_after_fork();
    message_results = false;
  }
  if (message_bindings) {
    internal::unlock_domain_bindings_after_fork();
    message_bindings = false;
  }
  if (child) {
    pid_t tid = caller->attributes.tid;
    for (unsigned i = 0; i < 3; ++i)
      if (standard_locked[i] &&
          !standard[i]->rebind_owner_after_fork(previous_tid, tid))
        internal::exit(127);
    for (auto *file = File::get_first_file(); file; file = file->get_next())
      if (file != standard[0] && file != standard[1] && file != standard[2] &&
          !file->rebind_owner_after_fork(previous_tid, tid))
        internal::exit(127);
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
  if (exit_callbacks) {
    handler_list_mtx.unlock();
    exit_callbacks = false;
  }
  if (priority) {
    if (child) {
      // Application mutex ownership is not transferred. Preserve the kernel's
      // inherited policy/priority, but do not apply the parent's lock ledger or
      // restoration debt to subsequent unrelated child scheduling operations.
      new (&caller->priority_protection) PriorityProtectionState{};
    } else if (caller->priority_protection.guard.unlock() != MutexError::NONE)
      __builtin_trap();
    priority = false;
  }
  if (child) {
    if (!child_recovery.allows_loader())
      caller->unwind_root.active = false;
    if (!terminal) {
      caller->cancellation.restore_after_fork();
      caller->cancellation_identity = caller->cancellation_generation = 0;
      caller->cancellation_point.store(nullptr);
      caller->cancellation_transfer.store(0);
    }
    restore_reaper_after_fork();
    once_after_fork(child_recovery.allows_fork());
    // Worker adopters establish phase, so select from the pre-clone certificate
    // rather than trying AFTER first and falling back to DURING.
    bool adopted = false;
    switch (continuation) {
    case ForkContinuation::Ordinary:
      adopted = internal::adopt_fork_survivor();
      break;
    case ForkContinuation::NormalDuring:
      adopted = internal::adopt_fork_survivor_during_cleanup(worker_cleanup);
      break;
    case ForkContinuation::NormalAfter:
      adopted = internal::adopt_fork_survivor_after_cleanup(worker_cleanup);
      break;
    case ForkContinuation::LastThreadAfter:
      adopted = internal::adopt_fork_survivor_after_thread_exit();
      break;
    case ForkContinuation::InitialThreadDuring:
#ifdef LIBC_MMIX_UNWIND_ROOTS
      adopted = internal::adopt_fork_survivor_during_thread_exit() &&
                rebind_initial_thread_during_fork_owner(previous_tid);
#endif
      break;
    }
    if (!adopted ||
        (continuation == ForkContinuation::LastThreadAfter &&
         (!internal::rebind_last_thread_exit_fork_owner ||
          !internal::rebind_last_thread_exit_fork_owner(previous_tid))) ||
        (process_cleanup && !terminal &&
         (!internal::rebind_normal_exit_fork_owner ||
          !internal::rebind_normal_exit_fork_owner(previous_tid))))
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
  if (child && child_recovery.allows_loader() &&
      (!restore_fork_unwind(*caller) ||
       (terminal &&
        (!continuation_root.matches(caller->unwind_root) ||
         (initial_during ? (!initial_thread_during_fork_owner() ||
                            !terminal_thread_fork_context(*caller))
                         : !last_thread_fork_context(*caller))))))
    internal::exit(127);
#endif
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  if (child && !terminal && child_recovery.allows_loader() &&
      !restore_fork_cancellation(*caller))
    internal::exit(127);
#endif
  if (credentials) {
    finish_credentials_fork();
    credentials = false;
  }
  if (notifications) {
    finish_notification_fork(child);
    if (child && process_cleanup && close_notification_admission)
      close_notification_admission();
    notifications = false;
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
