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
      (void)linux_syscalls::close(open->fd);
      auto fd = linux_syscalls::open(open->path, open->oflag, open->mode);
      if (!fd)
        child_failure(fd.error(), diagnostic);
      if (*fd != open->fd) {
        auto copied = linux_syscalls::dup2(*fd, open->fd);
        (void)linux_syscalls::close(*fd);
        if (!copied)
          child_failure(copied.error(), diagnostic);
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

} // namespace

int spawn(pid_t *pid, const char *const *paths,
          const posix_spawn_file_actions_t *actions,
          const posix_spawnattr_t *attr, char *const *argv, char *const *envp,
          SpawnErrorPolicy policy) {
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
    child_process(paths, actions, attr, saved, argv, envp, diagnostic);
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
