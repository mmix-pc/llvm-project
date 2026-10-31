//===-- Native MMIX last-thread root invariants ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Inject only cursor/kernel boundaries. This tests production root admission
// and fresh-generation capture, not a real MMIX forced-unwind continuation.
#include <cassert>
#include <csignal>
#include <linux/futex.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"
#include "src/__support/threads/linux/mmix/unwind_root.cpp"

namespace M = LIBC_NAMESPACE::mmix;
static unsigned window_initializations;
static bool cleanup_pending, dispatch_pending;
static uintptr_t expected_activation;
static uint64_t chain = 41;
struct Completed {};

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control;
bool has_c_cleanup() { return cleanup_pending; }
bool validate_fork_cleanup(const UnwindRoot &) { return !dispatch_pending; }
void dispatch_c_cleanup(uintptr_t, uintptr_t, uintptr_t) { __builtin_trap(); }
void initialize_cancellation_windows(ThreadControl &) {
  ++window_initializations;
}
void begin_thread_cleanup(ThreadControl &) { __builtin_trap(); }
[[noreturn]] void complete_thread_cleanup(ThreadControl &, ThreadReturnValue) {
  __builtin_trap();
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" int unw_getcontext(unw_context_t *) { return 0; }
extern "C" int unw_init_local(unw_cursor_t *, unw_context_t *) { return 0; }
extern "C" int unw_step(unw_cursor_t *) { return 1; }
extern "C" int unw_is_signal_frame(unw_cursor_t *) { return 0; }
extern "C" int unw_get_reg(unw_cursor_t *, int reg, unw_word_t *value) {
  *value = reg == UNW_MMIX_RO ? 0x1000 : 0x2000;
  return 0;
}
extern "C" int unw_get_proc_info(unw_cursor_t *, unw_proc_info_t *info) {
  info->start_ip = expected_activation;
  return 0;
}
extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long first,
                                         long second, long, long, long, long) {
  if (number == __NR_mmix_rstack_query) {
    reinterpret_cast<mmix_rstack_query *>(first)->chain_id = chain;
    return 0;
  }
  assert(number == SYS_futex && second == FUTEX_WAKE_PRIVATE);
  return 0;
}

static void ordinary_body(void *) {
  assert(M::current_control->unwind_root.active);
  assert(M::current_control->unwind_root.kind == M::UnwindRootKind::Thread);
}

static void terminal_body(void *) {
  auto &control = *M::current_control;
  assert(control.unwind_root.generation == 2);
  assert(control.unwind_root.procedure == expected_activation);
  assert(control.unwind_root.kind == M::UnwindRootKind::LastThreadProcess);
  assert(control.cancellation.terminal() && !control.cancellation.ready());
  assert(control.cancellation_identity == 9 &&
         control.cancellation_generation == 7);
  assert(window_initializations == 1);
  assert(M::last_thread_fork_context(control));
  assert(M::restore_fork_unwind(control));
  const auto saved = control.unwind_root;
  M::forced.active = true;
  assert(!M::last_thread_fork_context(control));
  assert(!M::restore_fork_unwind(control));
  M::forced.active = false;
  cleanup_pending = true;
  assert(!M::last_thread_fork_context(control));
  cleanup_pending = false;
  dispatch_pending = true;
  assert(!M::last_thread_fork_context(control));
  dispatch_pending = false;
  ++chain;
  assert(!M::last_thread_fork_context(control));
  --chain;
  control.unwind_root.owner = nullptr;
  assert(!M::last_thread_fork_context(control));
  control.unwind_root = saved;
  control.unwind_root.active = false;
  assert(!M::last_thread_fork_context(control));
  control.unwind_root = saved;
  control.unwind_root.kind = M::UnwindRootKind::Thread;
  assert(!M::last_thread_fork_context(control));
  control.unwind_root = saved;
  assert(M::last_thread_fork_context(control));
  throw Completed{}; // Test transport only; the production body never returns.
}

int main() {
  M::ThreadRegistry registry;
  M::ThreadControl control;
  control.attributes.tid = ::getpid();
  M::current_control = &control;
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.register_main(control));
  }
  expected_activation = reinterpret_cast<uintptr_t>(&M::run_with_unwind_root);
  M::run_with_unwind_root(control, ordinary_body, nullptr);
  assert(!control.unwind_root.active && control.unwind_root.generation == 1);
  assert(window_initializations == 1);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.begin_cleanup(control));
    assert(control.begin_callback_cleanup());
    assert(!control.terminal_cleanup_finished());
    assert(lock.close_leases(control) && lock.exit_ready(control, 42));
    assert(lock.termination_action(control) == M::ThreadTermination::Process);
  }
  control.cancellation_identity = 9;
  control.cancellation_generation = 7;
  expected_activation =
      reinterpret_cast<uintptr_t>(&M::run_with_process_exit_root);
  // None of these may overwrite a consumed/live root or escape to the body.
  for (unsigned bad = 0; bad != 6; ++bad) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      rlimit limit{0, 0};
      assert(::setrlimit(RLIMIT_CORE, &limit) == 0);
      ::alarm(5);
      if (bad == 0)
        M::forced.active = true;
      if (bad == 1)
        control.unwind_root.active = true;
      if (bad == 2)
        cleanup_pending = true;
      if (bad == 3)
        dispatch_pending = true;
      if (bad == 4)
        control.unwind_root.generation = UINT64_MAX;
      if (bad == 5)
        control.cancellation_transfer.store(1);
      M::run_with_process_exit_root(control, terminal_body, nullptr);
    }
    int status;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    assert(control.unwind_root.generation == 1 && !control.unwind_root.active);
    assert(!M::forced.active && !cleanup_pending && !dispatch_pending);
  }
  try {
    M::run_with_process_exit_root(control, terminal_body, nullptr);
  } catch (Completed &) {
    assert(control.terminal_cleanup_finished());
    assert(control.unwind_root.generation == 2 && window_initializations == 1);
    return 0;
  }
  __builtin_trap();
}
