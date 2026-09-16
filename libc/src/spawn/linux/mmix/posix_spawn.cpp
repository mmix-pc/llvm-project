//===-- MMIX Linux same-stack posix_spawn ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/spawn/posix_spawn.h"
#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/dup2.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/rt_sigprocmask.h"
#include "src/signal/linux/mmix/rt_sigaction.h"
#include "src/spawn/file_actions.h"

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE
#error "MMIX Linux posix_spawn currently requires single-thread mode"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace {

[[noreturn]] void child_process(const char *path,
                                const posix_spawn_file_actions_t *actions,
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
    if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) {
      reset.sa_handler = SIG_DFL;
      if (!mmix::rt_sigaction(signal, &reset, nullptr))
        internal::exit(127);
    }
  }

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
      if (!linux_syscalls::close(close->fd))
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
    }
  }
  if (!linux_syscalls::rt_sigprocmask(SIG_SETMASK, &saved, nullptr))
    internal::exit(127);
  (void)syscall_impl<long>(SYS_execve, path, argv, envp);
  // Exec must not return, including an unexpected nonnegative syscall result.
  internal::exit(127);
}

} // namespace

// FIXME: Reuse shared spawn policy after MMIX restorer selection and pthread
// coordination are supported. Only the null-attribute caller profile is ready.
LLVM_LIBC_FUNCTION(int, posix_spawn,
                   (pid_t *__restrict pid, const char *__restrict path,
                    const posix_spawn_file_actions_t *actions,
                    const posix_spawnattr_t *__restrict attr,
                    char *const *__restrict argv,
                    char *const *__restrict envp)) {
  if (attr)
    return ENOTSUP;
  sigset_t full{{-1UL}}, saved{};
  auto blocked = linux_syscalls::rt_sigprocmask(SIG_BLOCK, &full, &saved);
  if (!blocked)
    return blocked.error();

  // No at-fork callbacks: this child only performs raw setup, exec or exit.
  // The kernel copies the software and register stacks; both return normally.
  long result = syscall_impl<long>(SYS_clone, SIGCHLD, 0, 0, 0, 0);
  if (result == 0)
    child_process(path, actions, saved, argv, envp);
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

} // namespace LIBC_NAMESPACE_DECL
