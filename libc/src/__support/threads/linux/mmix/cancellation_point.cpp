//===-- MMIX Linux deferred syscall recovery ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cancellation_point.h"
#include "credentials.h"
#include "cancellation.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "lifecycle.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/thread.h"
#include "src/setjmp/setjmp_impl.h"
#include "src/signal/linux/mmix/kernel_sigaction.h"
#include "src/signal/linux/mmix/signal_trampoline.h"
#include "thread_finish.h"
#include "thread_join.h"
#include <asm/rstack.h>
#include <asm/sigcontext.h>
#include <asm/unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <ucontext.h>

#include "notification_validation.h"

extern "C" {
long __llvm_libc_mmix_cancel_leaf(unsigned long, long, long, long, long, long,
                                  long, void *);
void __llvm_libc_mmix_cancel_entry();
void __llvm_libc_mmix_cancel_after();
[[noreturn]] void __llvm_libc_mmix_jump_landing();
}

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
cpp::Atomic<uint64_t> identities{1};
long process_id = 0;
bool initialized = false;

void assign_cancellation_identity(ThreadControl &control) {
  control.cancellation_identity =
      identities.fetch_add(1, cpp::MemoryOrder::ACQ_REL);
  if (!control.cancellation_identity ||
      control.cancellation_identity == UINT64_MAX)
    __builtin_trap();
}
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
struct AsyncExit {
  __jmp_buf environment{};
  uint64_t identity = 0, generation = 0, mask = 0;
  uintptr_t ro = 0, sp = 0;
  int saved_errno = 0;
};
static_assert(__builtin_offsetof(ucontext_t, uc_mcontext) == 168);
LIBC_CONSTINIT LIBC_THREAD_LOCAL AsyncExit async_exit{};

extern "C" [[noreturn, gnu::noinline, clang::disable_tail_calls]] void
__llvm_libc_mmix_async_landing() {
  auto *control = current_control;
  mmix_rstack_query query{};
  if (!control || !control->cancellation.asynchronous() ||
      !control->unwind_root.active ||
      async_exit.identity != control->cancellation_identity ||
      async_exit.generation != control->unwind_root.generation ||
      syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) ||
      query.chain_id != control->unwind_root.chain ||
      control->cancellation_transfer.exchange(0, cpp::MemoryOrder::ACQ_REL) !=
          1 ||
      control->cancellation_suppression.fetch_sub(
          1, cpp::MemoryOrder::ACQ_REL) != 1)
    __builtin_trap();
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.begin_cleanup(*control, true))
      __builtin_trap();
  }
  libc_errno = async_exit.saved_errno;
  uint64_t mask = async_exit.mask | CANCEL_MASK;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&mask), 0, 8))
    __builtin_trap();
  dispatch_async_cleanup(async_exit.ro, async_exit.sp);
  complete_thread_cleanup(*control,
                          ThreadReturnValue(reinterpret_cast<void *>(-1L)));
}

[[noreturn]] void transfer_async(ThreadControl &control,
                                 const ucontext_t &context, int saved_errno) {
  const auto &saved =
      *reinterpret_cast<const sigcontext *>(&context.uc_mcontext);
  const auto &regs = saved.sc_regs;
  uint64_t expected = 0;
  if (!control.cancellation_transfer.compare_exchange_strong(
          expected, 1, cpp::MemoryOrder::ACQ_REL))
    __builtin_trap();
  control.cancellation_suppression.fetch_add(1, cpp::MemoryOrder::ACQ_REL);
  async_exit.identity = control.cancellation_identity;
  async_exit.generation = control.unwind_root.generation;
  async_exit.mask = *reinterpret_cast<const uint64_t *>(&context.uc_sigmask);
  async_exit.ro = regs.r_o;
  async_exit.sp = regs.regs[254];
  async_exit.saved_errno = saved_errno;
  auto &env = async_exit.environment;
  env.frame_pointer = regs.regs[253];
  env.return_address =
      reinterpret_cast<uintptr_t>(&__llvm_libc_mmix_async_landing);
  env.stack_pointer = regs.regs[254];
  env.register_stack_offset = regs.r_o;
  for (unsigned i = 0; i < 31; ++i)
    env.locals[i] = regs.regs[i];
  env.chain_id = saved.sc_chain_id;
  env.signal_mask = UINT64_MAX;
  env.save_mask = 1;
  mmix_rstack_jump request{
      saved.sc_chain_id,
      reinterpret_cast<uintptr_t>(&__llvm_libc_mmix_jump_landing),
      reinterpret_cast<uintptr_t>(&env),
      1,
      MMIX_RSTACK_JUMP_SETMASK,
      UINT64_MAX};
  syscall_impl(__NR_mmix_rstack_jump, reinterpret_cast<long>(&request));
  __builtin_trap();
}
#endif

uint64_t mask_all() {
  uint64_t full = UINT64_MAX, old;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&full), reinterpret_cast<long>(&old),
                   8))
    __builtin_trap();
  return old;
}
void restore_mask(uint64_t mask) {
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&mask), 0, 8))
    __builtin_trap();
}
void notification(int signal, siginfo_t *info, void *context) {
  int saved_errno = libc_errno;
  auto *control = current_control;
  if (!notification_origin_matches(signal, info, context, process_id) ||
      !control ||
      !control->cancellation.ready() ||
      control->cancellation_suppression.load(cpp::MemoryOrder::ACQUIRE) ||
      control->cancellation_transfer.load(cpp::MemoryOrder::ACQUIRE))
    return;
  auto *point = control->cancellation_point.load(cpp::MemoryOrder::ACQUIRE);
  // ucontext puts the machine context after a 168-byte fixed prefix.
  const auto &saved = *reinterpret_cast<const sigcontext *>(
      static_cast<const unsigned char *>(context) + 168);
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  if (control->cancellation.asynchronous()) {
    uintptr_t current_tp;
    asm volatile("OR %0, r230, 0" : "=r"(current_tp));
    if (!control->unwind_root.active ||
        saved.sc_chain_id != control->unwind_root.chain)
      return;
    if (control->unwind_root.owner != control || saved.sc_regs.regs[230] != current_tp ||
        saved.sc_regs.r_o < control->unwind_root.ro ||
        (saved.sc_regs.r_o & 7) || (saved.sc_regs.regs[254] & 7) ||
        !saved.sc_regs.regs[254] ||
        (!control->retains_resources_until_process_exit() &&
         (saved.sc_regs.regs[254] < control->stack_bottom ||
          saved.sc_regs.regs[254] >= control->stack_top)))
      __builtin_trap();
    transfer_async(*control, *static_cast<const ucontext_t *>(context),
                   saved_errno);
  }
#endif
  uintptr_t pc = saved.sc_regs.pc;
  if (!deferred_notification_context_matches(
          point, control, saved, pc,
          reinterpret_cast<uintptr_t>(&__llvm_libc_mmix_cancel_entry),
          reinterpret_cast<uintptr_t>(&__llvm_libc_mmix_cancel_after)))
    return;
  uint64_t expected = 0;
  if (!control->cancellation_transfer.compare_exchange_strong(
          expected, 1, cpp::MemoryOrder::ACQ_REL))
    return;
  if (control->cancellation_point.load(cpp::MemoryOrder::ACQUIRE) != point ||
      point->identity != control->cancellation_identity)
    __builtin_trap();
  control->cancellation_suppression.fetch_add(1, cpp::MemoryOrder::ACQ_REL);
  point->saved_errno = saved_errno;
  mmix_rstack_jump request{
      point->chain,
      reinterpret_cast<unsigned long>(&__llvm_libc_mmix_jump_landing),
      reinterpret_cast<unsigned long>(&point->environment[0]),
      1,
      MMIX_RSTACK_JUMP_SETMASK,
      UINT64_MAX};
  syscall_impl(__NR_mmix_rstack_jump, reinterpret_cast<long>(&request));
  // Any returned ancestor-transfer result is fatal, never a resumable error.
  __builtin_trap();
}
} // namespace

void initialize_cancellation_process() {
  uint64_t old = mask_all();
  if (initialized || !current_control ||
      !current_control->retains_resources_until_process_exit())
    __builtin_trap();
  process_id = syscall_impl(SYS_getpid);
  if (process_id <= 0)
    __builtin_trap();
  // Normalize inherited caught actions before any enabled user code runs.
  for (int signal = 1; signal < NSIG; ++signal) {
    if (signal == SIGKILL || signal == SIGSTOP || signal == CANCEL_SIGNAL ||
        signal == CREDENTIAL_SIGNAL)
      continue;
    KernelSigaction action{};
    if (syscall_impl(SYS_rt_sigaction, signal, 0,
                     reinterpret_cast<long>(&action), 8))
      __builtin_trap();
    uintptr_t handler = (action.sa_flags & SA_SIGINFO)
                            ? reinterpret_cast<uintptr_t>(action.sa_sigaction)
                            : reinterpret_cast<uintptr_t>(action.sa_handler);
    if (handler > 1) {
      action.sa_mask |= CANCEL_MASK;
      if (syscall_impl(SYS_rt_sigaction, signal,
                       reinterpret_cast<long>(&action), 0, 8))
        __builtin_trap();
    }
  }
  KernelSigaction action{};
  action.sa_sigaction = notification;
  action.sa_flags = SA_SIGINFO | SA_RESTART | SA_RESTORER;
  action.sa_restorer = __llvm_libc_mmix_signal_trampoline;
  action.sa_mask = UINT64_MAX;
  if (syscall_impl(SYS_rt_sigaction, CANCEL_SIGNAL,
                   reinterpret_cast<long>(&action), 0, 8))
    __builtin_trap();
  initialized = true;
  restore_mask(old);
}

void initialize_cancellation_windows(ThreadControl &control) {
  uint64_t old = mask_all();
  if (control.retains_resources_until_process_exit() && !initialized)
    initialize_cancellation_process();
  if (!initialized || (control.retains_resources_until_process_exit() &&
                       control.cancellation_identity))
    __builtin_trap();
  assign_cancellation_identity(control);
  restore_mask(control.cancellation.enabled() ? old & ~CANCEL_MASK : old);
}

bool restore_fork_cancellation(ThreadControl &control) {
  if (!initialized || current_control != &control ||
      !control.fork_recovery.allows_loader() || !control.unwind_root.active ||
      control.cancellation_identity || control.cancellation_point.load() ||
      control.cancellation_transfer.load() ||
      control.cancellation_suppression.load() != 1)
    return false;
  long child_pid = syscall_impl(SYS_getpid);
  if (child_pid <= 0)
    return false;
  // The action is inherited, but SI_TKILL now carries the child's process ID.
  // Do not rerun startup or unmask notifications before fork recovery finishes.
  process_id = child_pid;
  assign_cancellation_identity(control);
  return true;
}

bool cancellation_private_block() {
  auto *control = current_control;
  if (!control || !control->unwind_root.active ||
      control->cancellation.terminal() ||
      control->cancellation_suppression.load(cpp::MemoryOrder::ACQUIRE))
    return true;
  mmix_rstack_query query{};
  if (syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)))
    __builtin_trap();
  return query.chain_id != control->unwind_root.chain;
}

int cancellation_public_mask(int how, const uint64_t *set, uint64_t *old) {
  if (set && how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK)
    return EINVAL;
  uint64_t previous = mask_all(), next = previous;
  if (set) {
    uint64_t requested = *set & ~(CANCEL_MASK | CREDENTIAL_MASK);
    if (how == SIG_BLOCK)
      next |= requested;
    else if (how == SIG_UNBLOCK)
      next &= ~requested;
    else
      next = requested;
    next =
        cancellation_private_block() ? next | CANCEL_MASK : next & ~CANCEL_MASK;
  }
  if (old)
    *old = previous & ~(CANCEL_MASK | CREDENTIAL_MASK);
  restore_mask(next & ~CREDENTIAL_MASK);
  return 0;
}

[[gnu::noinline, clang::disable_tail_calls]]
long cancellation_syscall(unsigned long number, long a1, long a2, long a3,
                          long a4, long a5, long a6) {
  auto *control = current_control;
  if (!control || cancellation_private_block() ||
      !control->cancellation.enabled())
    return __llvm_libc_mmix_syscall(number, a1, a2, a3, a4, a5, a6);
  const uint64_t mask = mask_all();
  if (control->cancellation_generation == UINT64_MAX)
    __builtin_trap();
  CancellationPoint point{
      0,
      0,
      &control->cancellation,
      control,
      control->cancellation_point.load(cpp::MemoryOrder::ACQUIRE),
      control->cancellation_identity,
      ++control->cancellation_generation,
      control->unwind_root.chain,
      mask,
      libc_errno,
      {}};
  // The saved continuation is in this wrapper, not inside the raw leaf. Fields
  // changed by the leaf/handler are volatile across the returns-twice boundary.
  const int recovered = LIBC_NAMESPACE::setjmp(point.environment);
  long result = -4096;
  if (!recovered) {
    control->cancellation_point.store(&point, cpp::MemoryOrder::RELEASE);
    restore_mask(mask & ~CANCEL_MASK);
    result =
        __llvm_libc_mmix_cancel_leaf(number, a1, a2, a3, a4, a5, a6, &point);
    mask_all();
  }
  if (control->cancellation_point.load(cpp::MemoryOrder::ACQUIRE) != &point)
    __builtin_trap();
  control->cancellation_point.store(point.previous, cpp::MemoryOrder::RELEASE);
  if (recovered) {
    if (control->cancellation_transfer.exchange(0, cpp::MemoryOrder::ACQ_REL) !=
            1 ||
        control->cancellation_suppression.fetch_sub(
            1, cpp::MemoryOrder::ACQ_REL) != 1)
      __builtin_trap();
    libc_errno = point.saved_errno;
  }
  // Close errors may already consume the descriptor. Never retry or turn them
  // into cancellation; public errno translation happens after this decision.
  if (recovered || result == -4096 ||
      (result == -EINTR && number != SYS_close)) {
    bool claimed;
    {
      ThreadRegistryLock lock(thread_registry);
      claimed = lock.begin_cleanup(*control, true);
    }
    if (claimed)
      unwind_after_cleanup_claim(reinterpret_cast<void *>(-1L),
                                 mask | CANCEL_MASK);
    if (recovered || result == -4096)
      __builtin_trap();
  }
  restore_mask(mask);
  return result;
}

void await_cancellable_join(ThreadJoinClaim &claim) {
  if (!claim.control || claim.caller != internal::self.attrib)
    __builtin_trap();
  for (;;) {
    test_cancel();
    uint32_t value =
        claim.control->clear_tid.value.load(cpp::MemoryOrder::ACQUIRE);
    if (!value)
      return;
    // clear_child_tid wakeups use the shared futex key. Only this wait is
    // cancelable; claim release and successful reclamation remain raw paths.
    long ret = cancellation_syscall(
        SYS_futex, reinterpret_cast<long>(&claim.control->clear_tid.value.val),
        FUTEX_WAIT, value);
    if (ret != 0 && ret != -EINTR && ret != -EAGAIN)
      __builtin_trap();
  }
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
