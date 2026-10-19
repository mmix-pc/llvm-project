//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/system.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/sys_wait_macros.h"
#include "hdr/types/siginfo_t.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/rt_sigprocmask.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/wait4.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/mutex.h"
#include "src/signal/sigaction.h"
#include "src/unistd/environ.h"
#ifdef LIBC_MMIX_SYSTEM_COORDINATED
#include "src/__support/threads/linux/mmix/fork_runtime.h"
#else
#include "src/__support/threads/linux/mmix/process_operation.h"
#endif
#ifdef LIBC_MMIX_SYSTEM_CANCELLATION
#include "src/__support/threads/linux/mmix/cancellation.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <pthread.h>
#endif
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace {
Mutex disposition_mutex(false, false, false, false);
unsigned users;
struct sigaction original_int, original_quit;

class Command {
  bool dispositions = false, masked = false;
  sigset_t saved_mask{};
  struct sigaction child_int{}, child_quit{};
  pid_t child = -1;
#ifdef LIBC_MMIX_SYSTEM_CANCELLATION
  int old_state = PTHREAD_CANCEL_DISABLE;
  int old_type = PTHREAD_CANCEL_DEFERRED;
#endif

public:
  Command() {
#ifdef LIBC_MMIX_SYSTEM_CANCELLATION
    // Publish every owned resource before cancellation can unwind this scope.
    if (mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, &old_state) ||
        mmix::set_cancel_type(PTHREAD_CANCEL_DEFERRED, &old_type))
      __builtin_trap();
#endif
  }

  int prepare() {
    {
      cpp::lock_guard lock(disposition_mutex);
      if (!users) {
        struct sigaction ignore{};
        ignore.sa_handler = SIG_IGN;
        if (LIBC_NAMESPACE::sigaction(SIGINT, &ignore, &original_int))
          return libc_errno;
        if (LIBC_NAMESPACE::sigaction(SIGQUIT, &ignore, &original_quit)) {
          int error = libc_errno;
          if (LIBC_NAMESPACE::sigaction(SIGINT, &original_int, nullptr))
            __builtin_trap();
          return error;
        }
      }
      ++users;
      dispositions = true;
      child_int.sa_handler =
          original_int.sa_handler == SIG_IGN ? SIG_IGN : SIG_DFL;
      child_quit.sa_handler =
          original_quit.sa_handler == SIG_IGN ? SIG_IGN : SIG_DFL;
    }
    sigset_t blocked{};
    blocked.__signals[0] = 1UL << (SIGCHLD - 1);
    auto result =
        linux_syscalls::rt_sigprocmask(SIG_BLOCK, &blocked, &saved_mask);
    if (!result)
      return result.error();
    masked = true;
    return 0;
  }

  int launch(const char *command) {
#ifdef LIBC_MMIX_SYSTEM_COORDINATED
    mmix::ForkOperation operation;
    if (operation.error())
      return operation.error();
    int error = operation.prepare_runtime();
    if (!error)
      error = operation.freeze_registry();
    long result = error ? -error : operation.create_child();
    operation.restore_runtime(result == 0);
#else
    mmix::ProcessOperation operation;
    if (operation.error())
      return operation.error();
    long result = syscall_impl<long>(SYS_clone, SIGCHLD, 0, 0, 0, 0);
#endif
    if (!result) {
      // No user atfork callbacks or inherited handlers run in this exec-only
      // child. Only raw signal setup, exec and immediate exit are permitted.
      if (LIBC_NAMESPACE::sigaction(SIGINT, &child_int, nullptr) ||
          LIBC_NAMESPACE::sigaction(SIGQUIT, &child_quit, nullptr) ||
          !linux_syscalls::rt_sigprocmask(SIG_SETMASK, &saved_mask, nullptr))
        internal::exit(127);
      const char *argv[] = {"sh", "-c", "--", command, nullptr};
      (void)syscall_impl<long>(SYS_execve, "/bin/sh", argv, environ);
      internal::exit(127);
    }
    if (result < 0)
      return int(-result);
    child = pid_t(result);
    return 0;
  }

  int wait() {
#ifdef LIBC_MMIX_SYSTEM_CANCELLATION
    if (mmix::set_cancel_state(old_state, nullptr))
      __builtin_trap();
    // WNOWAIT keeps the PID owned until cancellation is disabled again. An
    // unwind after the syscall cannot kill a newly reused PID.
    siginfo_t info{};
    long result;
    do {
      result = mmix::cancellation_syscall(SYS_waitid, P_PID, child,
                                          reinterpret_cast<long>(&info),
                                          WEXITED | WNOWAIT, 0);
    } while (result == -EINTR);
    if (mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, nullptr))
      __builtin_trap();
    if (result < 0) {
      if (result == -ECHILD)
        child = -1;
      libc_errno = int(-result);
      return -1;
    }
#endif
    int status;
    auto waited = linux_syscalls::wait4(child, &status, 0, nullptr);
    while (!waited && waited.error() == EINTR)
      waited = linux_syscalls::wait4(child, &status, 0, nullptr);
    if (!waited) {
      if (waited.error() == ECHILD)
        child = -1;
      libc_errno = waited.error();
      return -1;
    }
    child = -1;
    return status;
  }

  ~Command() noexcept(false) {
    int error = libc_errno;
#ifdef LIBC_MMIX_SYSTEM_CANCELLATION
    if (mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, nullptr))
      __builtin_trap();
#endif
    if (child > 0) {
      (void)syscall_impl<long>(SYS_kill, child, SIGKILL);
      auto result = linux_syscalls::wait4(child, nullptr, 0, nullptr);
      while (!result && result.error() == EINTR)
        result = linux_syscalls::wait4(child, nullptr, 0, nullptr);
    }
    if (dispositions) {
      cpp::lock_guard lock(disposition_mutex);
      if (!--users) {
        if (LIBC_NAMESPACE::sigaction(SIGINT, &original_int, nullptr) ||
            LIBC_NAMESPACE::sigaction(SIGQUIT, &original_quit, nullptr))
          __builtin_trap();
      }
    }
    if (masked &&
        !linux_syscalls::rt_sigprocmask(SIG_SETMASK, &saved_mask, nullptr))
      __builtin_trap();
    libc_errno = error;
#ifdef LIBC_MMIX_SYSTEM_CANCELLATION
    if (mmix::set_cancel_type(old_type, nullptr) ||
        mmix::set_cancel_state(old_state, nullptr))
      __builtin_trap();
#endif
  }
};

int run(const char *command) {
  Command invocation;
  int error = invocation.prepare();
  if (!error)
    error = invocation.launch(command);
  if (error) {
    libc_errno = error;
    return -1;
  }
  return invocation.wait();
}
} // namespace

LLVM_LIBC_FUNCTION(int, system, (const char *command)) {
  // Probe the actual shell, including chroot/mount/execute restrictions.
  return command ? run(command) : run("exit 0") == 0;
}
} // namespace LIBC_NAMESPACE_DECL
