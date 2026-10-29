//===-- MMIX Linux same-stack posix_spawn ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/spawn/posix_spawn.h"
#include "hdr/errno_macros.h"
#include "hdr/types/struct_sched_param.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/dup2.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/rt_sigprocmask.h"
#ifdef LIBC_MMIX_SPAWN_COORDINATED
#include "coordinated_spawn.h"
#else
#include "src/__support/threads/linux/mmix/process_operation.h"
#endif
#include "src/signal/linux/mmix/private_signals.h"
#include "src/signal/linux/mmix/rt_sigaction.h"
#include "src/spawn/file_actions.h"
#include "src/spawn/linux/spawn.h"

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE &&                             \
    LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM
#error "MMIX Linux posix_spawn requires single-thread or platform mode"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace {

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
                                char *const *envp) {
  // Do not run inherited user handlers during child setup or before exec.
  // Ignored dispositions survive exec and must be preserved.
  for (int signal = 1; signal < NSIG; ++signal) {
    if (signal == SIGKILL || signal == SIGSTOP)
      continue;
    struct sigaction old{}, reset{};
    if (!mmix::rt_sigaction(signal, nullptr, &old))
      internal::exit(127);
    bool explicit_default =
        attr && (attr->__flags & POSIX_SPAWN_SETSIGDEF) &&
        (attr->__sigdefault.__signals[0] & (1UL << (signal - 1)));
    if (explicit_default ||
        (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN)) {
      reset.sa_handler = SIG_DFL;
      if (!mmix::rt_sigaction(signal, &reset, nullptr))
        internal::exit(127);
    }
  }

  if (apply_process_attributes(attr))
    internal::exit(127);

  auto *action =
      actions ? static_cast<BaseSpawnFileAction *>(actions->__front) : nullptr;
  for (; action; action = action->next) {
    switch (action->type) {
    case BaseSpawnFileAction::OPEN: {
      auto *open = static_cast<SpawnFileOpenAction *>(action);
      (void)linux_syscalls::close(open->fd);
      auto fd = linux_syscalls::open(open->path, open->oflag, open->mode);
      if (!fd)
        internal::exit(127);
      if (*fd != open->fd) {
        auto copied = linux_syscalls::dup2(*fd, open->fd);
        (void)linux_syscalls::close(*fd);
        if (!copied)
          internal::exit(127);
      }
      break;
    }
    case BaseSpawnFileAction::CLOSE: {
      auto *close = static_cast<SpawnFileCloseAction *>(action);
      auto result = linux_syscalls::close(close->fd);
      if (!result && result.error() != EBADF)
        internal::exit(127);
      break;
    }
    case BaseSpawnFileAction::DUP2: {
      auto *dup = static_cast<SpawnFileDup2Action *>(action);
      if (dup->fd == dup->newfd) {
        // Unlike dup2(fd, fd), a spawn action must clear close-on-exec.
        auto flags = linux_syscalls::fcntl(dup->fd, F_GETFD);
        if (!flags || syscall_impl<long>(SYS_fcntl, dup->fd, F_SETFD,
                                         *flags & ~FD_CLOEXEC) < 0)
          internal::exit(127);
      } else if (!linux_syscalls::dup2(dup->fd, dup->newfd)) {
        internal::exit(127);
      }
      break;
    }
    case BaseSpawnFileAction::CHDIR: {
      auto *change = static_cast<SpawnFileChdirAction *>(action);
      if (syscall_impl<long>(SYS_chdir, change->path) < 0)
        internal::exit(127);
      break;
    }
    case BaseSpawnFileAction::FCHDIR: {
      auto *change = static_cast<SpawnFileFchdirAction *>(action);
      if (syscall_impl<long>(SYS_fchdir, change->fd) < 0)
        internal::exit(127);
      break;
    }
    }
  }
  sigset_t mask = attr && (attr->__flags & POSIX_SPAWN_SETSIGMASK)
                      ? attr->__sigmask
                      : saved;
  mask.__signals[0] &= ~mmix::private_signal_mask;
  if (!linux_syscalls::rt_sigprocmask(SIG_SETMASK, &mask, nullptr))
    internal::exit(127);
  (void)spawn_exec(paths, argv, envp);
  // Exec must not return, including an unexpected nonnegative syscall result.
  internal::exit(127);
}

} // namespace

int spawn(pid_t *pid, const char *const *paths,
          const posix_spawn_file_actions_t *actions,
          const posix_spawnattr_t *attr, char *const *argv, char *const *envp) {
#ifndef LIBC_MMIX_SPAWN_COORDINATED
  mmix::ProcessOperation operation;
  if (operation.error())
    return operation.error();
#endif
  sigset_t full{{-1UL}}, saved{};
  auto blocked = linux_syscalls::rt_sigprocmask(SIG_BLOCK, &full, &saved);
  if (!blocked)
    return blocked.error();

  // No at-fork callbacks: this child only performs raw setup, exec or exit.
  // The kernel copies the software and register stacks; both return normally.
#ifdef LIBC_MMIX_SPAWN_COORDINATED
  long result = mmix::coordinated_spawn_clone();
#else
  long result = syscall_impl<long>(SYS_clone, SIGCHLD, 0, 0, 0, 0);
#endif
  if (result == 0)
    child_process(paths, actions, attr, saved, argv, envp);
  if (!linux_syscalls::rt_sigprocmask(SIG_SETMASK, &saved, nullptr))
    internal::exit(127);
  if (result < 0)
    return static_cast<int>(-result);
  if (pid)
    *pid = static_cast<pid_t>(result);
  // As in the shared provider, child setup/exec errors are reported by status
  // 127 on wait, not synchronously through a parent-side error pipe.
  return 0;
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
