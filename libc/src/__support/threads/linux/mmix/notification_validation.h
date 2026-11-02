//===-- MMIX Linux cancellation notification validation ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_VALIDATION_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_VALIDATION_H

#include "cancellation_point.h"
#include "hdr/signal_macros.h"
#include "hdr/types/jmp_buf.h"
#include "hdr/types/siginfo_t.h"
#include "lifecycle.h"
#include <asm/sigcontext.h>
#ifdef LIBC_MMIX_SIGNAL_WAIT
#include "src/signal/linux/mmix/signal_entry.h"
#endif

#if !defined(MMIX_SIGCONTEXT_HAS_CHAIN_ID) || MMIX_SIGCONTEXT_HAS_CHAIN_ID != 1
#error "MMIX cancellation windows require the matched interrupted-chain UAPI"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct CancellationPoint {
  volatile uintptr_t ro = 0, sp = 0;
  CancellationState *state;
  ThreadControl *owner;
  CancellationPoint *previous;
  uint64_t identity, generation, chain, mask;
  volatile int saved_errno;
  jmp_buf environment{};
  uint64_t signal_generation = 0;
};
static_assert(__builtin_offsetof(CancellationPoint, ro) == 0);
static_assert(__builtin_offsetof(CancellationPoint, sp) == 8);
static_assert(__builtin_offsetof(CancellationPoint, state) == 16);
static_assert(sizeof(CancellationState) == sizeof(uint64_t));
static_assert(__atomic_always_lock_free(sizeof(CancellationPoint *), nullptr));

// Borrow process identity so it is read only after the preceding origin checks.
[[gnu::always_inline]] LIBC_INLINE bool
notification_origin_matches(int signal, const siginfo_t *info,
                            const void *context, const long &process_id) {
  if (signal != CANCEL_SIGNAL || !info || !context ||
      info->si_signo != CANCEL_SIGNAL || info->si_code != SI_TKILL ||
      info->si_pid != process_id)
    return false;
  return true;
}

// The caller has already read PC, after state admission and any async branch.
// Keep descriptor/context reads conditional and in their original order.
[[gnu::always_inline]] LIBC_INLINE bool deferred_notification_context_matches(
    const CancellationPoint *point, const ThreadControl *control,
    const sigcontext &saved, uintptr_t pc, uintptr_t entry, uintptr_t after) {
  if (!point || point->owner != control ||
      point->identity != control->cancellation_identity || !point->generation ||
      point->generation > control->cancellation_generation ||
      !control->unwind_root.active ||
      saved.sc_chain_id != point->chain || saved.sc_regs.r_o != point->ro ||
      saved.sc_regs.regs[254] != point->sp || pc < entry || pc >= after)
    return false;
  if (!point->signal_generation)
    return point->chain == control->unwind_root.chain;
#ifdef LIBC_MMIX_SIGNAL_WAIT
  const auto *entry_record = current_signal_entry();
  return entry_record && entry_record->owner->thread == control &&
         entry_record->point.child_chain == point->chain &&
         entry_record->point.generation == point->signal_generation &&
         entry_record->owner->live_generation == point->signal_generation &&
         !entry_record->owner->handoff_generation;
#else
  return false;
#endif
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
