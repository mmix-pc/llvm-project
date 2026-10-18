//===-- Coordinated MMIX spawn regression ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/threads/linux/mmix/fork_runtime.h"
#include "src/pthread/pthread_atfork.h"
#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#include "src/signal/sigaction.h"
#include "src/signal/sigaddset.h"
#include "src/signal/sigemptyset.h"
#include "src/signal/sigismember.h"
#include "src/signal/sigprocmask.h"
#include "src/spawn/posix_spawn.h"
#include "src/spawn/posix_spawnp.h"
#include "src/sys/wait/waitpid.h"
#include "src/unistd/environ.h"
#include "test/IntegrationTest/test.h"
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>

namespace {
LIBC_NAMESPACE::cpp::Atomic<bool> ready(false), stop(false);
int callbacks = 0;
void callback() { ++callbacks; }
void handler(int) {}
void *worker(void *) {
  ready.store(true);
  while (!stop.load())
    (void)LIBC_NAMESPACE::syscall_impl<long>(SYS_sched_yield);
  return nullptr;
}
void check_mask() {
  sigset_t mask;
  ASSERT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, nullptr, &mask), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigismember(&mask, SIGUSR1), 1);
  ASSERT_EQ(LIBC_NAMESPACE::sigismember(&mask, SIGUSR2), 0);
}
void wait_for(pid_t pid, int expected) {
  int status;
  ASSERT_TRUE(pid > 0);
  ASSERT_EQ(LIBC_NAMESPACE::waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), expected);
}
} // namespace

TEST_MAIN(int argc, char **, char **) {
  if (argc == 2) {
    check_mask();
    struct sigaction action{};
    ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR1, nullptr, &action), 0);
    ASSERT_EQ(action.sa_handler, SIG_DFL);
    ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR2, nullptr, &action), 0);
    ASSERT_EQ(action.sa_handler, SIG_IGN);
    return 23;
  }

  ASSERT_EQ(LIBC_NAMESPACE::pthread_atfork(callback, callback, callback), 0);
  sigset_t mask{}, saved{};
  ASSERT_EQ(LIBC_NAMESPACE::sigemptyset(&mask), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaddset(&mask, SIGUSR1), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, &mask, &saved), 0);
  struct sigaction action{}, old_usr1{}, old_usr2{};
  action.sa_handler = handler;
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR1, &action, &old_usr1), 0);
  action.sa_handler = SIG_IGN;
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR2, &action, &old_usr2), 0);

  pthread_t thread;
  ASSERT_EQ(LIBC_NAMESPACE::pthread_create(&thread, nullptr, worker, nullptr),
            0);
  while (!ready.load())
    (void)LIBC_NAMESPACE::syscall_impl<long>(SYS_sched_yield);

  char zero[] = "spawn-child", child[] = "child";
  char *args[] = {zero, child, nullptr};
  for (int search = 0; search < 2; ++search) {
    pid_t pid = -1;
    int result = search ? LIBC_NAMESPACE::posix_spawnp(&pid, "/proc/self/exe",
                                                       nullptr, nullptr, args,
                                                       LIBC_NAMESPACE::environ)
                        : LIBC_NAMESPACE::posix_spawn(&pid, "/proc/self/exe",
                                                      nullptr, nullptr, args,
                                                      LIBC_NAMESPACE::environ);
    ASSERT_EQ(result, 0);
    check_mask();
    wait_for(pid, 23);
  }

  pid_t pid = -1;
  posix_spawnattr_t attr{};
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn(&pid, "/proc/self/exe", nullptr, &attr,
                                        args, LIBC_NAMESPACE::environ),
            ENOTSUP);
  ASSERT_EQ(pid, -1);
  check_mask();

  // Deterministic admission failure after spawn has saved/blocked the mask.
  // No fault injection: the real fork serializer rejects recursive admission.
  {
    LIBC_NAMESPACE::mmix::ForkOperation held;
    ASSERT_EQ(held.error(), 0);
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawn(&pid, "/proc/self/exe", nullptr,
                                          nullptr, args,
                                          LIBC_NAMESPACE::environ),
              EDEADLK);
    ASSERT_EQ(pid, -1);
    sigset_t blocked;
    ASSERT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, nullptr, &blocked), 0);
    ASSERT_EQ(LIBC_NAMESPACE::sigismember(&blocked, SIGUSR2), 1);
  }
  check_mask();
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn(&pid, "/no-such-spawn-executable",
                                        nullptr, nullptr, args,
                                        LIBC_NAMESPACE::environ),
            0);
  wait_for(pid, 127);
  check_mask();
  ASSERT_EQ(callbacks, 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR1, nullptr, &action), 0);
  ASSERT_EQ(action.sa_handler, handler);

  stop.store(true);
  ASSERT_EQ(LIBC_NAMESPACE::pthread_join(thread, nullptr), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR1, &old_usr1, nullptr), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGUSR2, &old_usr2, nullptr), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigprocmask(SIG_SETMASK, &saved, nullptr), 0);
  return 0;
}
