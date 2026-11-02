//===-- MMIX signal entry retirement ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_ENTRY_RECORD_H
#define LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_ENTRY_RECORD_H

#include "src/__support/CPP/atomic.h"
#include "src/__support/threads/linux/mmix/cleanup.h"
#include "src/__support/threads/linux/mmix/signal_boundary.h"
#include "src/setjmp/mmix/linux/jump_cleanup_stack.h"
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
#include "mmix/CursorLifetime.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct CancellationPoint;

// Only the default application entry publishes this derived record. Signals
// remain blocked through all transitions. Normal and nonlocal returns share
// the jump hook; a committed transfer must detach both lists before its stack
// disappears. The handoff itself lives outside this wrapper activation.
struct SignalEntryRecord : SignalBoundary {
  // Separate from the jump-observer checkpoint. Parent C records remain live
  // on their suspended chain and must never enter this segment's rO ordering.
  MmixCleanupLink parent_cleanup{};
  bool cleanup_tracked = false;
  // The parent publication remains live while this entry is linked. Compare
  // identities only; never follow an abandoned child's descriptor pointer.
  cpp::Atomic<CancellationPoint *> *cancellation_publication = nullptr;
  CancellationPoint *parent_cancellation = nullptr;
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
  MMIXCursorCheckpoint parent_cursor{};
#endif

private:
  JumpCleanupStack::Node jump;
  SignalParentHandoff *transfer = nullptr;

  bool cancellation_at_checkpoint() const {
    return !cancellation_publication ||
           cancellation_publication->load(cpp::MemoryOrder::ACQUIRE) ==
               parent_cancellation;
  }

  static bool retire(void *opaque) {
    auto &record = *static_cast<SignalEntryRecord *>(opaque);
    if (!record.owner || !record.cancellation_at_checkpoint())
      return false;
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
    if (!__libunwind_mmix_cursor_at_checkpoint(&record.parent_cursor))
      return false;
#endif
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
    if (record.cleanup_tracked &&
        !c_cleanup_at_checkpoint(record.parent_cleanup))
      return false;
#endif
    return record.transfer
               ? record.owner->detach(record, record.point, *record.transfer)
               : record.owner->normal_return(record, record.point);
  }

public:
  bool publish(SignalBoundaryOwner &owner, const SignalBoundaryPoint &point,
               uintptr_t context, JumpCleanupStack::Node *&head) {
    if (transfer || jump.linked() ||
        !point.wrapper.contains(this, sizeof(*this)) ||
        !owner.enter(*this, point, context, head, saved_parent))
      return false;
    if (!JumpCleanupStack::push(head, jump, retire, this))
      __builtin_trap();
    return true;
  }

  bool normal_return(JumpCleanupStack::Node *&head) {
    if (!cancellation_at_checkpoint())
      return false;
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
    if (!__libunwind_mmix_cursor_at_checkpoint(&parent_cursor))
      return false;
#endif
    return !transfer && JumpCleanupStack::pop(head, jump);
  }

  bool prepare_transfer(JumpCleanupStack::Node *&head,
                        const SignalBoundaryPoint &point,
                        const SignalBoundaryDisposal &disposal,
                        SignalParentHandoff &handoff) {
    // Do not discard active waits or guess that a callback flag proves their
    // removal. The live entry node must already be at the top of the stack.
    if (head != &jump || transfer || !owner || !cancellation_at_checkpoint() ||
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
        !__libunwind_mmix_cursor_at_checkpoint(&parent_cursor) ||
#endif
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
        (cleanup_tracked && !c_cleanup_at_checkpoint(parent_cleanup)) ||
#endif
        !owner->prepare_handoff(*this, point, disposal, handoff))
      return false;
    transfer = &handoff;
    if (!JumpCleanupStack::pop(head, jump) || head != cleanup_boundary)
      __builtin_trap();
    return true;
  }
};
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
