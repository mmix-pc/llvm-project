//===-- Active registered cleanup observations ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ACTIVE_CLEANUP_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ACTIVE_CLEANUP_H

#include "cleanup.h"
#include "cleanup_ownership.h"
#include "unwind_root.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

enum class ActiveCleanupKind : unsigned char { None, CDispatch, NormalPop };

// A borrowed identity, not an owner or a fork-recovery certificate. The
// executing record has already been consumed (its stored serial is zero).
// Static/non-image-owning dispatch leaves ownership entirely empty: it does
// not manufacture an Invocation or a lease. Neither this object nor its lease
// observations may be used to release, retain, relink, restart dispatch or
// manufacture a second Invocation.
struct ActiveCleanupWitness {
  ActiveCleanupKind kind = ActiveCleanupKind::None;
  const void *dispatch = nullptr;
  UnwindRoot root{};
  MmixCleanupLink executing{}, remaining{};
  uint64_t registration_serial = 0;
  uint64_t invocation_depth = 0;
  void (*callback)(void *) = nullptr;
  void *argument = nullptr;
  CleanupOwners::Witness ownership{};
};

// Owner thread, all application signals blocked throughout each call. The
// root must be the independently authenticated original Thread root. These
// operations check the actual kernel chain and live record/owner lists, but
// perform no metadata lookup, loader operation, mask change or ownership move.
// Failure leaves the output/state untouched; it does not distinguish absent
// from invalid activations and must never be treated as callback absence.
// Async dispatch and signal-origin callbacks have no such witness.
// Static/non-image-owning compositions validate
// activations and records without claiming image ownership.
// Shared success additionally requires one detached Invocation per observed
// callback activation (including suspended normal-pop callbacks) and one
// pending owner for each remaining record, including odd-serial records still
// owned by C++ RAII; an empty lease cannot substitute for shared ownership.
// Recheck after atfork prepare callbacks and before copying; an inspection
// failure must not reset dispatching or fall back to ordinary cleanup restore.
//
// NormalPop identifies registered C/C++ pop execution, NOT forced cleanup. Pair
// it with an independently authenticated ForcedCleanupWitness in Landing phase
// before drawing any forced-continuation conclusion. Direct/unregistered C++
// guards and ordinary destructors are outside this callback observation. Normal
// pop may register/pop nested records; capture observes the CURRENT serial and
// pending list, not a frozen callback-entry checkpoint. Recapture after changes.
// Suspended callbacks must retain their original remaining-list suffix. C stop
// dispatch still forbids registration. Borrow only during the same activation;
// equality is not an ABA/history guarantee after callbacks end.
bool snapshot_active_c_cleanup(const UnwindRoot &root,
                               ActiveCleanupWitness &out);
bool validate_active_c_cleanup(const UnwindRoot &root,
                               const ActiveCleanupWitness &witness);

// Positive evidence of callback absence, NOT a failed ActiveCleanupWitness.
// Ordinary destructors and direct/unregistered C++ guards have no activation
// here. This borrows the entire pending list without consuming any record or
// owner. Static compositions leave ownership empty and create no fake leases.
struct PendingCleanupWitness {
  UnwindRoot root{};
  MmixCleanupLink remaining{};
  uint64_t registration_serial = 0;
  CleanupOwners::PendingWitness ownership{};
};

// Same owner-thread, signal exclusion and independently authenticated Thread
// root preconditions as above. Success requires zero callback activations,
// neither synchronous nor async dispatch, and zero detached Invocations, plus
// validation of the actual chain and the complete pending record/owner list.
// Failure leaves output/state untouched and means invalid/unavailable, never
// callback absence. In particular, snapshot_active_c_cleanup failure does NOT
// select this case: only a successful snapshot_pending_cleanup can do so.
// Pair separately with a ForcedCleanupWitness in Landing phase for a forced
// ordinary destructor/direct guard; this API proves no forced state itself.
// Recapture after registration changes, including push/pop with the same head.
// This is an operation-scoped borrow, not an ABA/history or fork certificate.
bool snapshot_pending_cleanup(const UnwindRoot &root,
                              PendingCleanupWitness &out);
bool validate_pending_cleanup(const UnwindRoot &root,
                              const PendingCleanupWitness &witness);

// Future fork integration must separately prove the forced exception/phase,
// frame metadata, signal-policy quiescence, registry census, loader quiescence,
// cancellation scope and preserved stack/TLS backing. A same-address process
// copy preserves this witness AND any actual Invocation/owner/loader counts;
// no retain, count reset or token rebinding is implied. Restricted children
// must not resume/release inherited owners. No child admission is provided here.

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
