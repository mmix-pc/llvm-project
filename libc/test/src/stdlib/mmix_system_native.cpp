//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cassert>
#include <cerrno>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/__support/error_or.h"
// Inject only platform boundaries. The production Command implementation,
// signal-reference accounting and unwind cleanup are compiled unchanged.
#define LLVM_LIBC_SRC___SUPPORT_THREADS_MUTEX_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORK_RUNTIME_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_SYSCALL_WRAPPERS_RT_SIGPROCMASK_H
#define LIBC_MMIX_SYSTEM_COORDINATED
#define LIBC_MMIX_SYSTEM_CANCELLATION

static thread_local int failure;
static thread_local int slot = -1;
static std::atomic<int> ready[2], release_wait[2], child_pid[2];
static std::atomic<int> live_operations;
static bool cancel_after_observation;
static thread_local int wait_error;
namespace LIBC_NAMESPACE_DECL {
class Mutex {
  std::mutex mutex;

public:
  Mutex(bool, bool, bool, bool) {}
  void lock() { mutex.lock(); }
  void unlock() { mutex.unlock(); }
};
char **environ = ::environ;
int sigaction(int signal, const struct sigaction *action,
              struct sigaction *previous) {
  if (failure == 4 && signal == SIGQUIT && action &&
      action->sa_handler == SIG_IGN) {
    errno = EIO;
    return -1;
  }
  return ::sigaction(signal, action, previous);
}
namespace internal {
[[noreturn]] void exit(int status) { ::_exit(status); }
} // namespace internal
namespace linux_syscalls {
ErrorOr<int> rt_sigprocmask(int how, const sigset_t *set, sigset_t *old) {
  if (failure == 5 && how == SIG_BLOCK)
    return Error(EIO);
  int error = ::pthread_sigmask(how, set, old);
  return error ? ErrorOr<int>(Error(error)) : ErrorOr<int>(0);
}
} // namespace linux_syscalls
namespace mmix {
class ForkOperation {
public:
  ForkOperation() { ++live_operations; }
  ~ForkOperation() { --live_operations; }
  int error() { return failure == 1 ? EAGAIN : 0; }
  int prepare_runtime() { return failure == 2 ? ENOMEM : 0; }
  int freeze_registry() { return failure == 3 ? EAGAIN : 0; }
  void restore_runtime(bool) {}
};
int set_cancel_state(int state, int *old) {
  return ::pthread_setcancelstate(state, old);
}
int set_cancel_type(int type, int *old) {
  return ::pthread_setcanceltype(type, old);
}
long cancellation_syscall(unsigned long number, long a1, long a2, long a3,
                          long a4, long, long) {
  assert(number == SYS_waitid);
  if (wait_error) {
    int error = wait_error;
    wait_error = 0;
    return -error;
  }
  if (slot >= 0 && !cancel_after_observation) {
    child_pid[slot] = int(a2);
    ready[slot] = 1;
    while (!release_wait[slot])
      ::usleep(1000);
  }
  int result = ::waitid(idtype_t(a1), id_t(a2),
                        reinterpret_cast<siginfo_t *>(a3), int(a4));
  if (slot >= 0 && cancel_after_observation) {
    assert(result == 0);
    child_pid[slot] = int(a2);
    ready[slot] = 1;
    while (!release_wait[slot])
      ::usleep(1000);
  }
  return result == -1 ? -errno : result;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#define __signals __val
#include "src/stdlib/linux/mmix/system.cpp"
#undef __signals

static void handler(int) {}
static void check_dispositions(void (*expected)(int)) {
  struct sigaction action{};
  assert(::sigaction(SIGINT, nullptr, &action) == 0);
  assert(action.sa_handler == expected);
  assert(::sigaction(SIGQUIT, nullptr, &action) == 0);
  assert(action.sa_handler == expected);
}
static void *run(void *index) {
  slot = int(reinterpret_cast<intptr_t>(index));
  int status = LIBC_NAMESPACE::system(slot == 0 ? "exit 7" : "exit 9");
  return reinterpret_cast<void *>(intptr_t(status));
}
int main() {
  ::alarm(30);
  assert(LIBC_NAMESPACE::system(nullptr) == 1);
  assert(LIBC_NAMESPACE::system("exit 23") == (23 << 8));
  assert(LIBC_NAMESPACE::system("") == 0);
  assert(LIBC_NAMESPACE::system("exec /nonexistent-libc-system-command") ==
         (127 << 8));
  assert(LIBC_NAMESPACE::system("kill -TERM $$") == SIGTERM);
  wait_error = EINTR;
  assert(LIBC_NAMESPACE::system("exit 11") == (11 << 8));
  wait_error = EIO;
  assert(LIBC_NAMESPACE::system("exit 0") == -1 && errno == EIO);
  assert(::waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD);
  struct sigaction action{};
  action.sa_handler = handler;
  ::sigemptyset(&action.sa_mask);
  assert(::sigaction(SIGINT, &action, nullptr) == 0);
  assert(::sigaction(SIGQUIT, &action, nullptr) == 0);
  for (int i = 1; i <= 5; ++i) {
    failure = i;
    sigset_t before{}, after{};
    assert(::pthread_sigmask(SIG_SETMASK, nullptr, &before) == 0);
    assert(LIBC_NAMESPACE::system("exit 0") == -1);
    assert(errno == (i == 2 ? ENOMEM : i < 4 ? EAGAIN : EIO));
    assert(::pthread_sigmask(SIG_SETMASK, nullptr, &after) == 0);
    assert(::sigismember(&before, SIGCHLD) == ::sigismember(&after, SIGCHLD));
    check_dispositions(handler);
    assert(live_operations == 0);
  }
  failure = 0;
  // The child's caught disposition becomes default, not inherited SIG_IGN.
  assert(LIBC_NAMESPACE::system("kill -INT $$; exit 99") == SIGINT);
  check_dispositions(handler);
  action.sa_handler = SIG_IGN;
  assert(::sigaction(SIGINT, &action, nullptr) == 0);
  assert(LIBC_NAMESPACE::system("kill -INT $$; exit 19") == (19 << 8));
  action.sa_handler = handler;
  assert(::sigaction(SIGINT, &action, nullptr) == 0);

  pthread_t workers[2];
  for (int i = 0; i != 2; ++i)
    assert(::pthread_create(&workers[i], nullptr, run,
                            reinterpret_cast<void *>(intptr_t(i))) == 0);
  while (!ready[0] || !ready[1])
    ::usleep(1000);
  check_dispositions(SIG_IGN);
  release_wait[0] = 1;
  void *status;
  assert(::pthread_join(workers[0], &status) == 0);
  assert(intptr_t(status) == (7 << 8));
  check_dispositions(SIG_IGN);
  release_wait[1] = 1;
  assert(::pthread_join(workers[1], &status) == 0);
  assert(intptr_t(status) == (9 << 8));
  check_dispositions(handler);

  for (int after = 0; after != 2; ++after) {
    cancel_after_observation = after;
    ready[0] = release_wait[0] = 0;
    assert(::pthread_create(&workers[0], nullptr, run, nullptr) == 0);
    while (!ready[0])
      ::usleep(1000);
    // Cancellation before the WNOWAIT call still owns a child (possibly
    // zombie).
    assert(::pthread_cancel(workers[0]) == 0);
    assert(::pthread_join(workers[0], &status) == 0);
    assert(status == PTHREAD_CANCELED);
    errno = 0;
    assert(::waitpid(child_pid[0], nullptr, WNOHANG) == -1 && errno == ECHILD);
    check_dispositions(handler);
    assert(live_operations == 0);
  }
}
