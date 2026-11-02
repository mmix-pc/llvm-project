//===-- MMIX application signal boundary capture --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "signal_entry.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/threads/linux/mmix/lifecycle.h"
#include "src/setjmp/mmix/linux/jump_cleanup.h"
#include <asm/rstack.h>
#include <asm/unistd.h>
#include <mmix/Context.h>
#include <signal.h>
#include <ucontext.h>

#ifndef LIBC_MMIX_JUMP_CLEANUP
#error "Signal boundaries require matched jump cleanup providers"
#endif

extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_invoke_signal_handler(int, siginfo_t *, void *, uintptr_t);

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL SignalBoundaryOwner signal_boundary_owner{
    nullptr, 0};

namespace {
uint64_t block_signals() {
  uint64_t full = UINT64_MAX, previous;
  if (syscall_impl(__NR_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&full),
                   reinterpret_cast<long>(&previous), sizeof(full)))
    __builtin_trap();
  return previous;
}

void restore_signals(uint64_t mask) {
  if (syscall_impl(__NR_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&mask), 0, sizeof(mask)))
    __builtin_trap();
}

void capture_parent(unw_context_t &destination, const mcontext_t &context) {
  // Copy before calling application code. Never consult mutable ucontext again
  // to decide which activation or cleanup extent belongs to this record.
  libunwind::MMIXUnwindContext saved{};
  const auto &regs = context.sc_regs;
  __builtin_memcpy(saved.gpr, regs.regs, sizeof(saved.gpr));
  saved.rd = regs.r_d;
  saved.re = regs.r_e;
  saved.rh = regs.r_h;
  saved.rj = regs.r_j;
  saved.rr = regs.r_r;
  saved.ro = regs.r_o;
  saved.rg = regs.r_g;
  saved.rl = regs.r_l;
  saved.ip = regs.pc;
  static_assert(sizeof(saved) == sizeof(destination));
  __builtin_memcpy(&destination, &saved, sizeof(saved));
}
} // namespace

extern "C" __attribute__((noinline, disable_tail_calls, visibility("hidden")))
void __llvm_libc_mmix_application_signal_entry(int number, siginfo_t *info,
                                              void *opaque, uintptr_t handler,
                                              uintptr_t frame_limit) {
  uint64_t entry_mask = block_signals();
  SignalEntryRecord record;
  auto &owner = signal_boundary_owner;
  auto *control = current_control;
  auto *context = static_cast<ucontext_t *>(opaque);
  mmix_rstack_query query{};
  uintptr_t ro, sp;
  asm volatile("GET %0, rO\n\tOR %1, r254, 0" : "=r"(ro), "=r"(sp));
  bool entered = false;
  if (control && control->unwind_root.active &&
      control->unwind_root.owner == control &&
      !syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query))) {
    // A nested signal can arrive before an outer wrapper blocks signals, or
    // inside a custom restorer. Preserve delivery, but do not invent its missing
    // direct-parent record. Empty owners may follow a recovered fork root;
    // generations are never reset, even in that case.
    if (!owner.live && !owner.handoff_generation) {
      owner.thread = control;
      owner.base_chain = control->unwind_root.chain;
    }
    if (owner.generation != UINT64_MAX) {
      capture_parent(record.saved_parent, context->uc_mcontext);
      record.cancellation_publication = &control->cancellation_point;
      record.parent_cancellation =
          control->cancellation_point.load(cpp::MemoryOrder::ACQUIRE);
#ifdef LIBC_MMIX_SHARED_STARTUP
      __libunwind_mmix_cursor_checkpoint(&record.parent_cursor);
#endif
      SignalBoundaryPoint point{
          control, query.chain_id, context->uc_mcontext.sc_chain_id,
          owner.generation + 1,
          {ro, reinterpret_cast<uintptr_t>(
                   __llvm_libc_mmix_application_signal_entry), sp, frame_limit}};
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
      record.cleanup_tracked = snapshot_c_cleanup(record.parent_cleanup);
      if (record.cleanup_tracked)
#endif
      entered = record.publish(owner, point, reinterpret_cast<uintptr_t>(opaque),
                               jump_cleanup_head);
    }
  }
  restore_signals(entry_mask);
  __llvm_libc_mmix_invoke_signal_handler(number, info, opaque, handler);
  (void)block_signals();
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
  if (entered && !c_cleanup_at_checkpoint(record.parent_cleanup))
    __builtin_trap();
#endif
  if (entered && !record.normal_return(jump_cleanup_head))
    __builtin_trap();
  // Stay masked through the epilogue. rt_sigreturn restores uc_sigmask; an
  // earlier unmask would expose an active child after its record was removed.
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
