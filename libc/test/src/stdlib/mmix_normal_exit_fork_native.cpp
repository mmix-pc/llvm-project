//===-- Native MMIX normal-exit fork hook tests
//----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Standalone Linux x86-64 hook fixture, not coordinated-fork qualification.
// Example from the LLVM checkout (without LIBC_FULL_BUILD/public packaging):
// clang++ -std=c++17 -Ilibc -DLIBC_NAMESPACE=exit_fork_test
//   -DLIBC_THREAD_MODE=LIBC_THREAD_MODE_PLATFORM
//   -DLIBC_ERRNO_MODE=LIBC_ERRNO_MODE_THREAD_LOCAL
//   libc/test/src/stdlib/mmix_normal_exit_fork_native.cpp -o
//   /tmp/exit-fork-hooks
// Kernel masks/clear-TID, credentials, TLS drain and process callback dispatch
// are injected. Real host fork isolates cases and checks copied owner state.
// Ordinary-root proof and lifecycle/fork integration need separate target
// tests.

#include <cassert>
#include <csignal>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(LIBC_COPT_PUBLIC_PACKAGING) || defined(LIBC_MMIX_ROBUST_MUTEX) ||  \
    defined(LIBC_MMIX_CONCURRENT_STDIO)
#error "Use the standalone, non-public hook-test composition"
#endif

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"
#include "src/__support/threads/linux/mmix/main_thread.cpp"
#include "src/errno/libc_errno.cpp"
#define __cxa_finalize mmix_normal_exit_test_finalize
#include "src/stdlib/linux/mmix/exit.cpp"
#undef __cxa_finalize

namespace M = LIBC_NAMESPACE::mmix;
namespace I = LIBC_NAMESPACE::internal;

namespace {
enum class Case {
  Return,
  RecursiveProcessExit,
  RecursiveTLSExit,
  Terminal,
  Quick
};
Case selected;
struct ExitStatus {
  int value;
};
uint64_t mask = 0x4000;
unsigned tls_drains, process_drains, registrations;
bool notifications_closed;

void wait_success(pid_t child) {
  int status = 0;
  assert(::waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
class ThreadAtExitCallbackMgr {};
namespace mmix {
bool initialize_credentials(ThreadControl &) { return true; }
void close_notification_admission() { notifications_closed = true; }
} // namespace mmix
namespace internal {
ThreadAtExitCallbackMgr *get_thread_atexit_callback_mgr() {
  static ThreadAtExitCallbackMgr manager;
  return &manager;
}
void call_atexit_callbacks(ThreadAttributes *attributes) {
  assert(attributes == self.attrib && ++tls_drains == 1);
  assert(!main_thread_cleanup_finished() && !normal_exit_fork_owner());
  assert(!rebind_normal_exit_fork_owner(attributes->tid));
  assert(!adopt_fork_survivor_after_cleanup());
  if (selected == Case::RecursiveTLSExit)
    exit_process(29);
}
[[noreturn]] void exit(int status) { throw ExitStatus{status}; }
} // namespace internal

extern "C" void mmix_normal_exit_test_finalize(void *dso) {
  assert(!dso && ++process_drains == 1 && tls_drains == 1);
  assert(I::main_thread_cleanup_finished() && notifications_closed);
  auto &caller = *M::current_control;
  pid_t owner = caller.attributes.tid;
  assert(I::normal_exit_fork_owner() == owner);
  assert(!I::rebind_normal_exit_fork_owner(owner)); // Still the parent.
  assert(!I::adopt_fork_survivor_after_cleanup());
  if (selected == Case::RecursiveProcessExit)
    I::exit_process(31);
  if (selected == Case::Terminal) {
    assert(caller.cancellation.claim(false));
    assert(!I::normal_exit_fork_owner());
    assert(!I::rebind_normal_exit_fork_owner(owner));
    return;
  }

  // A worker must not borrow main's completed cleanup phase or owner marker.
  M::ThreadControl worker;
  worker.attributes.tid = owner;
  M::current_control = &worker;
  I::self.attrib = &worker.attributes;
  assert(!I::main_thread_cleanup_finished() && !I::normal_exit_fork_owner());
  M::current_control = &caller;
  I::self.attrib = &caller.attributes;

  pid_t child = ::fork();
  assert(child >= 0);
  if (!child) {
    ::alarm(10);
    caller.attributes.tid = ::getpid();
    assert(!I::normal_exit_fork_owner()); // Copied owner still names parent.
    assert(!I::rebind_normal_exit_fork_owner(owner)); // Not yet recovered.
    caller.fork_recovery = I::ForkRecovery::child();
    assert(!I::rebind_normal_exit_fork_owner(owner)); // ExecOnly is excluded.
    assert(!I::adopt_fork_survivor_after_cleanup());
    // Model only the private recovery capability. Actual registry restoration
    // and its admission certificate have separate lifecycle/guest tests.
    caller.fork_recovery = I::ForkRecovery::child(true, true);
    assert(!I::rebind_normal_exit_fork_owner(0));
    assert(!I::rebind_normal_exit_fork_owner(caller.attributes.tid));
    assert(I::rebind_normal_exit_fork_owner(owner));
    assert(!I::rebind_normal_exit_fork_owner(owner)); // No second transfer.
    assert(I::normal_exit_fork_owner() == caller.attributes.tid);
    assert(I::adopt_fork_survivor_after_cleanup());
    assert(I::main_thread_cleanup_finished() && registrations == 2);
    I::cleanup_current_thread();
    assert(tls_drains == 1 && process_drains == 1);
    assert(!M::thread_registry.claim_process_cleanup());
    return; // Resume the copied exit_process; no recursive exit call.
  }
  wait_success(child);
  assert(caller.attributes.tid == owner &&
         I::normal_exit_fork_owner() == owner);
  assert(registrations == 1 && I::main_thread_cleanup_finished());
  assert(!M::thread_registry.claim_process_cleanup());
}
} // namespace LIBC_NAMESPACE_DECL

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long first,
                                         long second, long third, long fourth,
                                         long, long) {
  if (number == SYS_futex) {
    assert(second == FUTEX_WAKE_PRIVATE);
    return 0;
  }
  if (number == SYS_set_tid_address) {
    assert(first != 0 && M::current_control);
    ++registrations;
    return M::current_control->attributes.tid;
  }
  assert(number == SYS_rt_sigprocmask && first == SIG_SETMASK && fourth == 8);
  if (third)
    *reinterpret_cast<uint64_t *>(third) = mask;
  mask = *reinterpret_cast<const uint64_t *>(second);
  return 0;
}

static void exercise(Case test) {
  selected = test;
  M::ThreadControl caller;
  caller.attributes.tid = ::getpid();
  // activate_main_thread publishes current_control after the clear-TID call;
  // the injected kernel needs the planned TID for that first call too.
  M::current_control = &caller;
  assert(I::activate_main_thread(caller));
  assert(!I::main_thread_cleanup_finished() && !I::normal_exit_fork_owner());
  assert(!I::adopt_fork_survivor_after_cleanup());
  if (test == Case::Quick) {
    I::begin_process_exit(0, true);
    I::cleanup_current_thread();
    assert(I::main_thread_cleanup_finished());
    assert(!I::normal_exit_fork_owner());
    assert(!I::rebind_normal_exit_fork_owner(caller.attributes.tid));
    return;
  }
  try {
    I::exit_process(23);
  } catch (const ExitStatus &status) {
    int expected = test == Case::RecursiveTLSExit       ? 29
                   : test == Case::RecursiveProcessExit ? 31
                                                        : 23;
    assert(status.value == expected);
    assert(tls_drains == 1);
    assert(process_drains == (test == Case::RecursiveTLSExit ? 0u : 1u));
    if (test == Case::Return || test == Case::Terminal)
      assert(!I::normal_exit_fork_owner()); // Finalization scope has ended.
    return;
  }
  __builtin_trap();
}

int main() {
  const Case cases[] = {Case::Return, Case::RecursiveProcessExit,
                        Case::RecursiveTLSExit, Case::Terminal, Case::Quick};
  for (Case test : cases) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      ::alarm(10);
      exercise(test);
      ::_exit(0);
    }
    wait_success(child);
  }
}
