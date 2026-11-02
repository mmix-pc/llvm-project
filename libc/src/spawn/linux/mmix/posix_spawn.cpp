//===-- MMIX Linux same-stack posix_spawn ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/spawn/posix_spawn.h"
#include "hdr/errno_macros.h"
#include "hdr/sys_mman_macros.h"
#include "hdr/types/struct_sched_param.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/dup2.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/rt_sigprocmask.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/wait4.h"
#include "src/__support/threads/linux/mmix/credentials.h"
#ifdef LIBC_MMIX_SPAWN_EVENT
#include "spawn_event.h"
#include "src/__support/threads/linux/mmix/fork_runtime.h"
#include "src/__support/threads/linux/mmix/lifecycle.h"
#include "src/__support/threads/linux/mmix/thread_finish.h"
#include "src/__support/threads/linux/mmix/thread_reaper.h"
#include "src/__support/threads/linux/mmix/unwind_root.h"
#include <sys/wait.h>
#endif
#ifdef LIBC_MMIX_SPAWN_COORDINATED
#include "coordinated_spawn.h"
#else
#include "src/__support/threads/linux/mmix/process_operation.h"
#endif
#include "src/signal/linux/mmix/private_signals.h"
#include "src/signal/linux/mmix/rt_sigaction.h"
#include "src/spawn/file_actions.h"
#include "src/spawn/linux/spawn.h"
#include <linux/sched.h>

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE &&                             \
    LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM
#error "MMIX Linux posix_spawn requires single-thread or platform mode"
#endif

namespace LIBC_NAMESPACE_DECL {

#ifdef LIBC_MMIX_SPAWN_EVENT
namespace internal {
pid_t last_thread_exit_fork_owner() __attribute__((weak));
}
#endif

SpawnSignalMask::SpawnSignalMask() {
  // PATH preparation has no cancellation points or application callbacks.
  // Blocking delivery protects allocation without adding suppression nesting
  // that would violate the inner provider's independent admission checks.
  sigset_t mask{{UINT64_MAX}};
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  mask.__signals[0] &= ~mmix::CREDENTIAL_MASK;
#endif
  auto result = linux_syscalls::rt_sigprocmask(SIG_SETMASK, &mask, &saved);
  if (!result)
    error_code = result.error();
}

SpawnSignalMask::~SpawnSignalMask() noexcept(false) {
  if (!error_code &&
      !linux_syscalls::rt_sigprocmask(SIG_SETMASK, &saved, nullptr))
    __builtin_trap();
}

namespace {

[[noreturn]] void child_failure(int error, int *diagnostic) {
  if (diagnostic)
    __atomic_store_n(diagnostic, error, __ATOMIC_RELEASE);
  internal::exit(127);
}

int apply_process_attributes(const posix_spawnattr_t *attr) {
  if (!attr)
    return 0;
  long result;
  if (attr->__flags & POSIX_SPAWN_SETSCHEDULER) {
    struct sched_param param{attr->__priority};
    result =
        syscall_impl<long>(SYS_sched_setscheduler, 0, attr->__policy, &param);
    if (result < 0)
      return static_cast<int>(-result);
  } else if (attr->__flags & POSIX_SPAWN_SETSCHEDPARAM) {
    struct sched_param param{attr->__priority};
    result = syscall_impl<long>(SYS_sched_setparam, 0, &param);
    if (result < 0)
      return static_cast<int>(-result);
  }
  if (attr->__flags & POSIX_SPAWN_SETSID) {
    result = syscall_impl<long>(SYS_setsid);
    if (result < 0)
      return static_cast<int>(-result);
  }
  if (attr->__flags & POSIX_SPAWN_SETPGROUP) {
    result = syscall_impl<long>(SYS_setpgid, 0, attr->__pgrp);
    if (result < 0)
      return static_cast<int>(-result);
  }
  if (attr->__flags & POSIX_SPAWN_RESETIDS) {
    // This private, single-task child must not broadcast to copied peers.
    long gid = syscall_impl<long>(SYS_getgid);
    result = syscall_impl<long>(SYS_setresgid, -1, gid, -1);
    if (result < 0)
      return static_cast<int>(-result);
    long uid = syscall_impl<long>(SYS_getuid);
    result = syscall_impl<long>(SYS_setresuid, -1, uid, -1);
    if (result < 0)
      return static_cast<int>(-result);
  }
  return 0;
}

[[noreturn]] void child_process(const char *const *paths,
                                const posix_spawn_file_actions_t *actions,
                                const posix_spawnattr_t *attr,
                                const sigset_t &saved, char *const *argv,
                                char *const *envp, int *diagnostic) {
  // Do not run inherited user handlers during child setup or before exec.
  // Ignored dispositions survive exec and must be preserved.
  for (int signal = 1; signal < NSIG; ++signal) {
    if (signal == SIGKILL || signal == SIGSTOP)
      continue;
    struct sigaction old{}, reset{};
    auto queried = mmix::rt_sigaction(signal, nullptr, &old);
    if (!queried)
      child_failure(queried.error(), diagnostic);
    bool explicit_default =
        attr && (attr->__flags & POSIX_SPAWN_SETSIGDEF) &&
        (attr->__sigdefault.__signals[0] & (1UL << (signal - 1)));
    if (explicit_default ||
        (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN)) {
      reset.sa_handler = SIG_DFL;
      auto changed = mmix::rt_sigaction(signal, &reset, nullptr);
      if (!changed)
        child_failure(changed.error(), diagnostic);
    }
  }

  if (int error = apply_process_attributes(attr))
    child_failure(error, diagnostic);

  auto *action =
      actions ? static_cast<BaseSpawnFileAction *>(actions->__front) : nullptr;
  for (; action; action = action->next) {
    switch (action->type) {
    case BaseSpawnFileAction::OPEN: {
      auto *open = static_cast<SpawnFileOpenAction *>(action);
      auto closed = linux_syscalls::close(open->fd);
      if (!closed && closed.error() != EBADF)
        child_failure(closed.error(), diagnostic);
      auto fd = linux_syscalls::open(open->path, open->oflag, open->mode);
      if (!fd)
        child_failure(fd.error(), diagnostic);
      if (*fd != open->fd) {
        auto copied = linux_syscalls::dup2(*fd, open->fd);
        auto temporary_closed = linux_syscalls::close(*fd);
        if (!copied)
          child_failure(copied.error(), diagnostic);
        if (!temporary_closed)
          child_failure(temporary_closed.error(), diagnostic);
      }
      break;
    }
    case BaseSpawnFileAction::CLOSE: {
      auto *close = static_cast<SpawnFileCloseAction *>(action);
      auto result = linux_syscalls::close(close->fd);
      if (!result && result.error() != EBADF)
        child_failure(result.error(), diagnostic);
      break;
    }
    case BaseSpawnFileAction::DUP2: {
      auto *dup = static_cast<SpawnFileDup2Action *>(action);
      if (dup->fd == dup->newfd) {
        // Unlike dup2(fd, fd), a spawn action must clear close-on-exec.
        auto flags = linux_syscalls::fcntl(dup->fd, F_GETFD);
        if (!flags)
          child_failure(flags.error(), diagnostic);
        long result = syscall_impl<long>(SYS_fcntl, dup->fd, F_SETFD,
                                         *flags & ~FD_CLOEXEC);
        if (result < 0)
          child_failure(static_cast<int>(-result), diagnostic);
      } else {
        auto result = linux_syscalls::dup2(dup->fd, dup->newfd);
        if (!result)
          child_failure(result.error(), diagnostic);
      }
      break;
    }
    case BaseSpawnFileAction::CHDIR: {
      auto *change = static_cast<SpawnFileChdirAction *>(action);
      long result = syscall_impl<long>(SYS_chdir, change->path);
      if (result < 0)
        child_failure(static_cast<int>(-result), diagnostic);
      break;
    }
    case BaseSpawnFileAction::FCHDIR: {
      auto *change = static_cast<SpawnFileFchdirAction *>(action);
      long result = syscall_impl<long>(SYS_fchdir, change->fd);
      if (result < 0)
        child_failure(static_cast<int>(-result), diagnostic);
      break;
    }
    }
  }
  sigset_t mask = attr && (attr->__flags & POSIX_SPAWN_SETSIGMASK)
                      ? attr->__sigmask
                      : saved;
  mask.__signals[0] &= ~mmix::private_signal_mask;
  auto restored = linux_syscalls::rt_sigprocmask(SIG_SETMASK, &mask, nullptr);
  if (!restored)
    child_failure(restored.error(), diagnostic);
  // Exec must not return, including an unexpected nonnegative syscall result.
  child_failure(spawn_exec(paths, argv, envp), diagnostic);
}

#ifdef LIBC_MMIX_SPAWN_EVENT
bool terminal_spawn_admitted(mmix::ThreadControl &caller) {
#ifdef LIBC_MMIX_UNWIND_ROOTS
  auto continuation = mmix::ForkContinuation::Ordinary;
  if (internal::last_thread_exit_fork_owner &&
      internal::last_thread_exit_fork_owner() == caller.attributes.tid) {
    if (mmix::last_thread_fork_context(caller))
      continuation = mmix::ForkContinuation::LastThreadAfter;
    else if (mmix::last_worker_fork_context(caller))
      continuation = mmix::ForkContinuation::LastWorkerAfter;
  } else if (mmix::initial_thread_during_fork_owner() ==
                 caller.attributes.tid &&
             mmix::terminal_thread_fork_context(caller)) {
    continuation = mmix::ForkContinuation::InitialThreadDuring;
  } else if (mmix::worker_thread_during_fork_owner() == caller.attributes.tid &&
             mmix::terminal_worker_fork_context(caller)) {
    continuation = mmix::ForkContinuation::WorkerThreadDuring;
  }
  // DURING needs a valid owner, not helper retirement: this child goes directly
  // to exec/_exit and never resumes the copied terminal activation.
  if (continuation == mmix::ForkContinuation::InitialThreadDuring ||
      continuation == mmix::ForkContinuation::WorkerThreadDuring) {
    if (!mmix::thread_registry.freeze_for_fork(
            caller, 1, nullptr, false, continuation,
            mmix::ForkSnapshot::TerminalExecOnly))
      return false;
    mmix::thread_registry.release_after_fork();
    return true;
  }
  // AFTER retains its stronger certificate. Release all inspection locks
  // before creating or waiting for the raw child.
  return continuation != mmix::ForkContinuation::Ordinary &&
         mmix::reaper_absent_for_exit_fork(caller, continuation);
#else
  (void)caller;
  return false;
#endif
}

// Coordinated public spawn waits on an event without runtime snapshot locks.
// ChildStatus keeps the asynchronous path used by popen.
class SpawnParent {
  mmix::ThreadControl *caller = nullptr;
  bool masked = false, suppressed = false;

public:
  sigset_t saved{};
  int error = 0;

  SpawnParent() {
    if (mmix::ForkOperation::active_on_this_thread()) {
      error = EDEADLK;
      return;
    }
    caller = mmix::current_control;
    if (!caller || !caller->fork_recovery.allows_fork() ||
        !caller->credential_registered) {
      error = ENOTSUP;
      return;
    }
    sigset_t mask{{UINT64_MAX & ~mmix::CREDENTIAL_MASK}};
    auto result = linux_syscalls::rt_sigprocmask(SIG_SETMASK, &mask, &saved);
    if (!result) {
      error = result.error();
      return;
    }
    masked = true;
    if (caller->cancellation_transfer.load() ||
        caller->cancellation_point.load() ||
        caller->cancellation_suppression.load()) {
      error = EAGAIN;
      return;
    }
    caller->cancellation_suppression.fetch_add(1);
    suppressed = true;
    if (caller->cancellation.terminal() && !terminal_spawn_admitted(*caller))
      error = EAGAIN;
  }

  ~SpawnParent() noexcept(false) {
    if (suppressed && caller->cancellation_suppression.fetch_sub(1) != 1)
      __builtin_trap();
    // Pending asynchronous delivery is restored only after result publication
    // and all parent-owned mappings/descriptors have been consumed.
    if (masked &&
        !linux_syscalls::rt_sigprocmask(SIG_SETMASK, &saved, nullptr))
      __builtin_trap();
  }
};

int event_spawn(pid_t *pid, const char *const *paths,
                const posix_spawn_file_actions_t *actions,
                const posix_spawnattr_t *attr, char *const *argv,
                char *const *envp, const sigset_t *child_mask) {
  SpawnParent parent;
  if (parent.error)
    return parent.error;
  long capability = mmix::spawn_event_capability();
  if (capability == -ENOSYS ||
      (capability >= 0 && !(capability & MMIX_SPAWN_CAP_V1)))
    return ENOTSUP;
  if (capability < 0)
    return int(-capability);
  long mapping = syscall_impl<long>(SYS_mmap, 0, sizeof(int),
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (mapping < 0 && mapping >= -4095)
    return int(-mapping);
  auto *diagnostic = reinterpret_cast<int *>(mapping);
  mmix::SpawnEventHandles handles;
  long result;
  {
    // No allocator, stream, registry, loader or fork snapshot locks. The
    // copied child never destroys this guard or touches copied runtime state.
    mmix::CredentialGate credentials;
    result = mmix::create_spawn_event(handles);
    if (!result)
      child_process(paths, actions, attr,
                    child_mask ? *child_mask : parent.saved, argv, envp,
                    diagnostic);
  }
  int error = result < 0 ? int(-result) : 0;
  if (result > 0) {
    long observed;
    do {
      observed = mmix::wait_spawn_event(handles.event_fd);
    } while (observed == -EINTR);
    // The v1 contract admits only success/EINTR for a valid owned event.
    // Invalid/replaced private fds or syscall-policy sabotage violate that
    // contract; do not turn them into an ordinary error abandoning a child.
    if (observed)
      __builtin_trap();
    error = __atomic_load_n(diagnostic, __ATOMIC_ACQUIRE);
    if (error) {
      siginfo_t info{};
      long reaped;
      do {
        reaped = syscall_impl<long>(SYS_waitid, P_PIDFD, handles.pidfd, &info,
                                    WEXITED, nullptr);
      } while (reaped == -EINTR);
      if (reaped && reaped != -ECHILD)
        __builtin_trap();
    }
    // Linux close consumes even on EINTR. Failed CREATE did not grant either
    // descriptor, including any numbers written to its private scratch slots.
    (void)linux_syscalls::close(handles.event_fd);
    (void)linux_syscalls::close(handles.pidfd);
  }
  if (syscall_impl<long>(SYS_munmap, diagnostic, sizeof(int)))
    __builtin_trap();
  if (!error && pid)
    *pid = static_cast<pid_t>(result);
  return error;
}
#endif

int spawn_impl(pid_t *pid, const char *const *paths,
               const posix_spawn_file_actions_t *actions,
               const posix_spawnattr_t *attr, char *const *argv,
               char *const *envp, SpawnErrorPolicy policy,
               const sigset_t *child_mask) {
#ifdef LIBC_MMIX_SPAWN_EVENT
  if (policy == SpawnErrorPolicy::ProviderDefault)
    return event_spawn(pid, paths, actions, attr, argv, envp, child_mask);
#endif
#ifndef LIBC_MMIX_SPAWN_COORDINATED
  mmix::ProcessOperation operation;
  if (operation.error())
    return operation.error();
#endif
  sigset_t full{{-1UL}}, saved{};
  auto blocked = linux_syscalls::rt_sigprocmask(SIG_BLOCK, &full, &saved);
  if (!blocked)
    return blocked.error();

  bool synchronous = policy == SpawnErrorPolicy::ProviderDefault;
  int *diagnostic = nullptr;
  int error = 0;
  if (synchronous) {
    long mapping =
        syscall_impl<long>(SYS_mmap, 0, sizeof(int), PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mapping < 0 && mapping >= -4095)
      error = static_cast<int>(-mapping);
    else
      diagnostic = reinterpret_cast<int *>(mapping);
  }

  // No at-fork callbacks: this child only performs raw setup, exec or exit.
  // The kernel copies the software and register stacks; both return normally.
#ifdef LIBC_MMIX_SPAWN_COORDINATED
  long result = error ? -error : mmix::coordinated_spawn_clone(synchronous);
#else
  long result =
      error ? -error
            : syscall_impl<long>(SYS_clone,
                                 SIGCHLD | (synchronous ? CLONE_VFORK : 0), 0,
                                 0, 0, 0);
#endif
  if (result == 0)
    child_process(paths, actions, attr, child_mask ? *child_mask : saved, argv,
                  envp, diagnostic);
  if (result < 0)
    error = static_cast<int>(-result);
  else if (diagnostic)
    error = __atomic_load_n(diagnostic, __ATOMIC_ACQUIRE);

  if (diagnostic) {
    // The copied-VM completion has retired this child's mapping. No inherited
    // descriptor in an unrelated process can prolong the diagnostic protocol.
    if (syscall_impl<long>(SYS_munmap, diagnostic, sizeof(int)) < 0)
      internal::exit(127);
    if (result > 0 && error) {
      for (;;) {
        auto reaped = linux_syscalls::wait4(static_cast<pid_t>(result), nullptr,
                                            0, nullptr);
        if (reaped || reaped.error() == ECHILD)
          break; // Includes SIGCHLD auto-reaping or an application waiter.
        if (reaped.error() != EINTR)
          internal::exit(127);
      }
    }
  }
  if (!error && pid)
    *pid = static_cast<pid_t>(result);
  if (!linux_syscalls::rt_sigprocmask(SIG_SETMASK, &saved, nullptr))
    internal::exit(127);
  return error;
}

} // namespace

int spawn(pid_t *pid, const char *const *paths,
          const posix_spawn_file_actions_t *actions,
          const posix_spawnattr_t *attr, char *const *argv, char *const *envp,
          SpawnErrorPolicy policy) {
  return spawn_impl(pid, paths, actions, attr, argv, envp, policy, nullptr);
}

int spawn(pid_t *pid, const char *const *paths,
          const posix_spawn_file_actions_t *actions,
          const posix_spawnattr_t *attr, char *const *argv, char *const *envp,
          const SpawnSignalMask &mask) {
  return spawn_impl(pid, paths, actions, attr, argv, envp,
                    SpawnErrorPolicy::ProviderDefault, &mask.original());
}

LLVM_LIBC_FUNCTION(int, posix_spawn,
                   (pid_t *__restrict pid, const char *__restrict path,
                    const posix_spawn_file_actions_t *actions,
                    const posix_spawnattr_t *__restrict attr,
                    char *const *__restrict argv,
                    char *const *__restrict envp)) {
  const char *paths[] = {path, nullptr};
  return spawn(pid, paths, actions, attr, argv, envp);
}

} // namespace LIBC_NAMESPACE_DECL
