//===-- MMIX private cursor ownership ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_CURSOR_LIFETIME_H
#define LIBUNWIND_MMIX_CURSOR_LIFETIME_H

#include <stdbool.h>
#include <stdint.h>

// Stack-owned sidecar. This is not part of unw_cursor_t or unw_context_t.
// An internal cursor is registered before initialization and released on scope
// exit. Raw public copies remain outside this protocol and use resident views.
// With _LIBUNWIND_MMIX_CURSOR_LIFETIME alone, images are immutable ET_EXEC:
// token stays zero, but owners, queries and checkpoints follow the same protocol.
struct MMIXCursorLifetime {
  void *cursor;
  uintptr_t token;
  struct MMIXCursorLifetime *previous;
  uintptr_t fallback_token;
  // Private root capture borrows the permanently prepared startup image. It
  // cannot be copied into a general cursor or used for forced traversal.
  bool root_image;
};

// A borrowed checkpoint in a still-live, suspended parent. Capture with signals
// blocked and retain it only until that activation returns or is abandoned.
// It does not retain an image or authenticate an arbitrary copied pointer.
struct MMIXCursorCheckpoint {
  uintptr_t thread;
  struct MMIXCursorLifetime *owners;
  void *query;
  void *forced_exception;
  unsigned forced_phase;
};

enum MMIXForcedCheckpointPhase {
  MMIXForcedStopped = 1,
  MMIXForcedLanding = 2
};

// Borrowed identities only, not leases or permission to copy/resume an unwind.
// This private interface requires matching rebuilt consumers and provider; it
// does not change the public cursor/context or the signal checkpoint layout.
struct MMIXForcedCheckpoint {
  uintptr_t thread;
  void *exception;
  uintptr_t stop;
  void *argument;
  struct MMIXCursorLifetime *base;
  struct MMIXCursorLifetime *owner;
  void *cursor;
  uintptr_t token;
  uintptr_t fallback_token;
  uintptr_t held_token;
  uintptr_t held_fallback_token;
  unsigned phase;
};

#ifdef __cplusplus
extern "C" {
#endif
// Imported by libc as well as used internally; protection belongs to the
// provider definitions, not to declarations in another shared object.
void __libunwind_mmix_cursor_begin(struct MMIXCursorLifetime *, void *);
void __libunwind_mmix_cursor_begin_root(struct MMIXCursorLifetime *, void *);
void __libunwind_mmix_cursor_end(struct MMIXCursorLifetime *);
void __libunwind_mmix_cursor_finish(void *);
void __libunwind_mmix_cursor_checkpoint(struct MMIXCursorCheckpoint *);
int __libunwind_mmix_cursor_at_checkpoint(const struct MMIXCursorCheckpoint *);
// Only retire the one managed child cursor above the captured parent. Reject
// an active query, extra child owners, a public/untracked cursor or a foreign
// checkpoint without mutation. Success releases, never escape-pins, any lease.
// The caller must not read cursor metadata again before the ancestor transfer.
int __libunwind_mmix_cursor_finish_at(void *, const struct MMIXCursorCheckpoint *);
void __libunwind_mmix_set_managed_stop(uintptr_t);
// Read-only observation of the externally authenticated exception/stop/argument.
// Base must be this thread's empty, query-free, pre-forced checkpoint. Stopped
// requires exactly one live phase-2 owner above base and no held tokens. Landing
// requires that cursor already retired, no owners and the persistent held slots.
// Both snapshot and fallback identities are captured, including legitimate zero
// tokens in static mode. Reject all other phases, queries and owner shapes.
// Return 1 on success, 0 on refusal (capture leaves output unchanged). Validate
// compares every saved field with a fresh observation, never dereferencing saved
// pointers/tokens. Neither call queries metadata, locks, retains/releases tokens,
// mutates runtime state, or supplies fork admission/continuation authority.
// Caller must exclude signals and keep the same forced activation suspended;
// discard the borrow when it advances or ends. Equality is not an ABA/history
// check and does not authenticate exception contents, roots or loader accounting.
int __libunwind_mmix_forced_capture(const struct MMIXCursorCheckpoint *base,
                                  void *exception, uintptr_t stop,
                                  void *argument,
                                  struct MMIXForcedCheckpoint *output);
int __libunwind_mmix_forced_validate(const struct MMIXCursorCheckpoint *base,
                                   void *exception, uintptr_t stop,
                                   void *argument,
                                   const struct MMIXForcedCheckpoint *saved);
// Restricted copied active fork child only: validate the exact existing borrow,
// then atomically quarantine this continuation. Caller must exclude signals and
// hold libc's scoped fork witness; this hook does not authenticate fork state.
// Return 1 on transition, 0 without mutation on mismatch. Only the private phase
// changes: no cursor/owner/token mutation, metadata access, locks or allocation.
// Quarantine is terminal, retaining all copied leases until process exit. Attempts
// to resume this exception or consume its owner trap without unwind diagnostics.
int __libunwind_mmix_forced_quarantine(const struct MMIXCursorCheckpoint *base,
                                     void *exception, uintptr_t stop,
                                     void *argument,
                                     const struct MMIXForcedCheckpoint *saved);
// Managed forced-unwind hooks. A landing pad owns the old snapshot without a
// stack cursor; resume moves it back before the new cursor's metadata lookup.
// Other stop functions are deliberately outside this private protocol.
__attribute__((visibility("hidden"))) int
__libunwind_mmix_forced_resume(void *, uintptr_t, void *);
__attribute__((visibility("hidden"))) int
__libunwind_mmix_forced_enter(void *, void *, uintptr_t, void *);
__attribute__((visibility("hidden"))) int
__libunwind_mmix_forced_stop(void *, void *, uintptr_t, void *, int);
__attribute__((visibility("hidden"))) int
__libunwind_mmix_forced_landing(void *, void *, uintptr_t, void *);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_forced_abort(void *, uintptr_t, void *);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_cursor_begin_with_stop(struct MMIXCursorLifetime *, void *,
                                        uintptr_t);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_cursor_escape(void *);
__attribute__((visibility("hidden"))) int
__libunwind_mmix_cursor_copy(void *, const void *);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_cursor_reset(void *);
__attribute__((visibility("hidden"))) void *
__libunwind_mmix_cursor_query_enter(void *);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_cursor_query_leave(void *);
#ifdef __cplusplus
}
// Preserve the standalone shared-provider default used by native fixtures.
// The explicit static opt-in omits image interfaces; startup images take priority.
#if defined(_LIBUNWIND_MMIX_STARTUP_IMAGES) ||                                  \
    !defined(_LIBUNWIND_MMIX_CURSOR_LIFETIME)
#include "StartupImages.h"
bool mmixFindCursorImage(uintptr_t, MMIXUnwindImage &, bool &owned);
#endif
class MMIXCursorQuery {
  void *previous;

public:
  explicit MMIXCursorQuery(void *cursor)
      : previous(__libunwind_mmix_cursor_query_enter(cursor)) {}
  MMIXCursorQuery(const MMIXCursorQuery &) = delete;
  MMIXCursorQuery &operator=(const MMIXCursorQuery &) = delete;
  ~MMIXCursorQuery() { __libunwind_mmix_cursor_query_leave(previous); }
};
#endif
#endif
