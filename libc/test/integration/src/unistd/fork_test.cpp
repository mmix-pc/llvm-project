//===-- Unittests for fork ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/syscall.h"
#include "src/pthread/pthread_atfork.h"
#include "src/signal/raise.h"
#include "src/stdlib/exit.h"
#include "src/sys/wait/wait.h"
#include "src/sys/wait/wait4.h"
#include "src/sys/wait/waitpid.h"
#include "src/unistd/fork.h"
#include "src/unistd/gettid.h"

#include "test/IntegrationTest/test.h"

#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

// The tests wait4 and waitpid are present as tests for those functions
// really and not for the fork function. They are here along with the tests
// for fork because it is convenient to invoke and test them after forking
// a child.

void fork_and_wait_normal_exit() {
  pid_t pid = LIBC_NAMESPACE::fork();
  if (pid == 0)
    return; // Just end without any thing special.
  ASSERT_TRUE(pid > 0);
  int status;
  pid_t cpid = LIBC_NAMESPACE::wait(&status);
  ASSERT_TRUE(cpid > 0);
  ASSERT_EQ(cpid, pid);
  ASSERT_TRUE(WIFEXITED(status));
}

void fork_and_wait4_normal_exit() {
  pid_t pid = LIBC_NAMESPACE::fork();
  if (pid == 0)
    return; // Just end without any thing special.
  ASSERT_TRUE(pid > 0);
  int status;
  struct rusage usage;
  usage.ru_utime = {0, 0};
  usage.ru_stime = {0, 0};
  pid_t cpid = LIBC_NAMESPACE::wait4(pid, &status, 0, &usage);
  ASSERT_TRUE(cpid > 0);
  ASSERT_EQ(cpid, pid);
  ASSERT_TRUE(WIFEXITED(status));
}

void fork_and_waitpid_normal_exit() {
  pid_t pid = LIBC_NAMESPACE::fork();
  if (pid == 0)
    return; // Just end without any thing special.
  ASSERT_TRUE(pid > 0);
  int status;
  pid_t cpid = LIBC_NAMESPACE::waitpid(pid, &status, 0);
  ASSERT_TRUE(cpid > 0);
  ASSERT_EQ(cpid, pid);
  ASSERT_TRUE(WIFEXITED(status));
}

void fork_and_wait_signal_exit() {
  pid_t pid = LIBC_NAMESPACE::fork();
  if (pid == 0)
    LIBC_NAMESPACE::raise(SIGUSR1);
  ASSERT_TRUE(pid > 0);
  int status;
  pid_t cpid = LIBC_NAMESPACE::wait(&status);
  ASSERT_TRUE(cpid > 0);
  ASSERT_EQ(cpid, pid);
  ASSERT_FALSE(WIFEXITED(status));
  ASSERT_TRUE(WTERMSIG(status) == SIGUSR1);
}

void fork_and_wait4_signal_exit() {
  pid_t pid = LIBC_NAMESPACE::fork();
  if (pid == 0)
    LIBC_NAMESPACE::raise(SIGUSR1);
  ASSERT_TRUE(pid > 0);
  int status;
  struct rusage usage;
  usage.ru_utime = {0, 0};
  usage.ru_stime = {0, 0};
  pid_t cpid = LIBC_NAMESPACE::wait4(pid, &status, 0, &usage);
  ASSERT_TRUE(cpid > 0);
  ASSERT_EQ(cpid, pid);
  ASSERT_FALSE(WIFEXITED(status));
  ASSERT_TRUE(WTERMSIG(status) == SIGUSR1);
}

void fork_and_waitpid_signal_exit() {
  pid_t pid = LIBC_NAMESPACE::fork();
  if (pid == 0)
    LIBC_NAMESPACE::raise(SIGUSR1);
  ASSERT_TRUE(pid > 0);
  int status;
  pid_t cpid = LIBC_NAMESPACE::waitpid(pid, &status, 0);
  ASSERT_TRUE(cpid > 0);
  ASSERT_EQ(cpid, pid);
  ASSERT_FALSE(WIFEXITED(status));
  ASSERT_TRUE(WTERMSIG(status) == SIGUSR1);
}

static int prepare = 0;
static int parent = 0;
static int child = 0;
static constexpr int DONE = 0x600D;

static void prepare_cb() { prepare = DONE; }

static void parent_cb() { parent = DONE; }

static void child_cb() { child = DONE; }

void fork_with_atfork_callbacks() {
  ASSERT_EQ(LIBC_NAMESPACE::pthread_atfork(&prepare_cb, &parent_cb, &child_cb),
            0);
  pid_t pid = LIBC_NAMESPACE::fork();
  if (pid == 0) {
    // Raise a signal from the child if unexpected at-fork
    // behavior is observed.
    if (child != DONE || prepare != DONE || parent == DONE)
      LIBC_NAMESPACE::raise(SIGUSR1);
    return;
  }

  ASSERT_TRUE(pid > 0);
  int status;
  pid_t cpid = LIBC_NAMESPACE::waitpid(pid, &status, 0);
  ASSERT_TRUE(cpid > 0);
  ASSERT_EQ(cpid, pid);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(prepare, DONE);
  ASSERT_EQ(parent, DONE);
  ASSERT_NE(child, DONE);
}

void gettid_test() {
  // fork and verify tid is consistent with the syscall result.
  int pid = LIBC_NAMESPACE::fork();
  ASSERT_EQ(LIBC_NAMESPACE::gettid(),
            LIBC_NAMESPACE::syscall_impl<pid_t>(SYS_gettid));
  if (pid == 0)
    LIBC_NAMESPACE::exit(0);
  // make sure child process exits normally
  int status;
  pid_t cpid = LIBC_NAMESPACE::waitpid(pid, &status, 0);
  ASSERT_TRUE(cpid > 0);
  ASSERT_EQ(cpid, pid);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static long long callback_order;
static bool register_from_callbacks;
static bool registered_prepare_callback;
static bool registered_post_callback;

static void prepare_four() { callback_order = callback_order * 10 + 4; }
static void post_four() { callback_order = callback_order * 10 + 4; }
static void prepare_five() { callback_order = callback_order * 10 + 5; }
static void post_five() { callback_order = callback_order * 10 + 5; }
static void prepare_one() { callback_order = callback_order * 10 + 1; }
static void post_one() { callback_order = callback_order * 10 + 1; }
static void prepare_two() {
  callback_order = callback_order * 10 + 2;
  if (register_from_callbacks && !registered_prepare_callback) {
    ASSERT_EQ(
        LIBC_NAMESPACE::pthread_atfork(prepare_four, post_four, post_four), 0);
    registered_prepare_callback = true;
  }
}
static void post_two() {
  callback_order = callback_order * 10 + 2;
  if (register_from_callbacks && !registered_post_callback) {
    ASSERT_EQ(
        LIBC_NAMESPACE::pthread_atfork(prepare_five, post_five, post_five), 0);
    registered_post_callback = true;
  }
}
static void prepare_three() { callback_order = callback_order * 10 + 3; }
static void post_three() { callback_order = callback_order * 10 + 3; }

void fork_with_ordered_reentrant_callbacks() {
  ASSERT_EQ(LIBC_NAMESPACE::pthread_atfork(prepare_one, post_one, post_one), 0);
  ASSERT_EQ(LIBC_NAMESPACE::pthread_atfork(nullptr, nullptr, nullptr), 0);
  ASSERT_EQ(LIBC_NAMESPACE::pthread_atfork(prepare_two, post_two, post_two), 0);
  ASSERT_EQ(
      LIBC_NAMESPACE::pthread_atfork(prepare_three, post_three, post_three), 0);

  for (int round = 0; round < 3; ++round) {
    callback_order = 0;
    register_from_callbacks = round != 0;
    pid_t pid = LIBC_NAMESPACE::fork();
    // The same frozen set runs in both processes. Registrations made by
    // prepare, parent or child must not extend the current traversal or
    // deadlock it.
    ASSERT_EQ(callback_order, round < 2 ? 321123 : 5432112345LL);
    if (pid == 0)
      LIBC_NAMESPACE::internal::exit(0);
    ASSERT_TRUE(pid > 0);
    int status;
    ASSERT_EQ(LIBC_NAMESPACE::waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
  }
}

TEST_MAIN([[maybe_unused]] int argc, [[maybe_unused]] char **argv,
          [[maybe_unused]] char **envp) {
  gettid_test();
  fork_and_wait_normal_exit();
  fork_and_wait4_normal_exit();
  fork_and_waitpid_normal_exit();
  fork_and_wait_signal_exit();
  fork_and_wait4_signal_exit();
  fork_and_waitpid_signal_exit();
  fork_with_atfork_callbacks();
  fork_with_ordered_reentrant_callbacks();
  return 0;
}
