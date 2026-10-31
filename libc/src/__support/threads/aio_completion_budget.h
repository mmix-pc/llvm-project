//===-- Private AIO completion credits -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_COMPLETION_BUDGET_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_COMPLETION_BUDGET_H

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Caller-serialized CQ accounting, not request state or a public AIO provider.
// All access, including queries and publication/completion bookkeeping, needs
// the service lock. No allocation, locking, token lookup or automatic release.
//
// One budget covers EVERY CQE producer on one ring. Reserve one Credit for each
// original operation and a separate Credit for each cancel/control command.
// Only single-shot operations producing exactly one CQE are supported: no
// multishot, skipped CQEs or unaccounted auxiliary CQEs. A cancel CQE releases
// only its own credit, regardless of its result or the original's state.
// No control headroom is implicit: limit original admission if controls must
// remain issuable at saturation; otherwise control reservation can get EAGAIN.
//
// Tokens are nonzero and never reused within this budget, even after rollback.
// Use them as SQE user_data and as AioRequest/CancelControl tokens; maintain
// the trusted token-to-holder binding in the service. Route events by their
// owning ring and process before calling consume(). Independent budgets can
// issue the same numbers; numeric equality alone does not authenticate an
// event.
//
// Keep the budget and each held Credit alive at stable addresses until release.
// Destroying a holder does not refund credit. Ring teardown, lost completions
// and protocol errors require owner containment, not synthetic consumption.
class AioCompletionBudget {
public:
  class Credit {
    friend class AioCompletionBudget;
    AioCompletionBudget *owner = nullptr;
    uint64_t token_value = 0;
    bool published = false;

  public:
    LIBC_INLINE constexpr Credit() = default;
    Credit(const Credit &) = delete;
    Credit &operator=(const Credit &) = delete;

    LIBC_INLINE uint64_t token() const { return token_value; }
    LIBC_INLINE bool reclaimable() const { return owner == nullptr; }
  };

private:
  uint32_t limit = 0;
  uint32_t held = 0;
  uint64_t last_token;

  LIBC_INLINE void release(Credit &credit) {
    credit.owner = nullptr;
    credit.token_value = 0;
    credit.published = false;
    --held;
  }

public:
  // Preserve this watermark across drained service restarts in the same token
  // namespace. Never replace a live budget or lower its watermark. Injection
  // also permits deterministic exhaustion tests without wrapping the counter.
  LIBC_INLINE explicit constexpr AioCompletionBudget(
      uint64_t last_issued_token = 0)
      : last_token(last_issued_token) {}
  AioCompletionBudget(const AioCompletionBudget &) = delete;
  AioCompletionBudget &operator=(const AioCompletionBudget &) = delete;

  // Configure once, before any admission. The caller must prove this bound is
  // <= the actual CQ capacity of an empty, exclusively owned ring. The current
  // Ring does not expose negotiated geometry; its successful initialize(n)
  // guarantees CQ >= SQ >= n, so n is a safe conservative bound. Never assume
  // CQ == 2 * SQ. A conservative bound need not itself be a power of two.
  // 2^30 is the largest power-of-two capacity accepted by the transport.
  LIBC_INLINE ErrorOr<void> initialize(uint32_t completion_capacity) {
    if (limit != 0)
      return Error(EBUSY);
    if (completion_capacity == 0 || completion_capacity > (uint32_t(1) << 30))
      return Error(EINVAL);
    limit = completion_capacity;
    return {};
  }

  LIBC_INLINE uint32_t capacity() const { return limit; }
  LIBC_INLINE uint32_t outstanding() const { return held; }
  LIBC_INLINE uint32_t available() const { return limit - held; }
  LIBC_INLINE uint64_t last_issued_token() const { return last_token; }

  // Identity for a local result that can never produce a CQE (for example a
  // rejected list member). It shares the namespace but consumes no credit.
  LIBC_INLINE ErrorOr<uint64_t> reserve_local_token() {
    if (!limit)
      return Error(EINVAL);
    if (last_token == UINT64_MAX)
      return Error(EOVERFLOW);
    return ++last_token;
  }

  // Reserve before any SQ publication or request acceptance. Failure changes
  // neither the holder nor the watermark. EAGAIN is capacity backpressure;
  // EOVERFLOW is permanent token exhaustion, even after every credit drains.
  LIBC_INLINE ErrorOr<uint64_t> reserve(Credit &credit) {
    if (limit == 0 || !credit.reclaimable())
      return Error(EINVAL);
    if (last_token == UINT64_MAX)
      return Error(EOVERFLOW);
    if (held == limit)
      return Error(EAGAIN);
    credit.owner = this;
    credit.token_value = ++last_token;
    credit.published = false;
    ++held;
    return credit.token_value;
  }

  // After Ring::enqueue succeeds, call this under the SAME service lock,
  // before any consumer can dispatch its CQE. It cannot fail for a reserved,
  // unpublished holder owned by this budget. enqueue failure publishes nothing
  // and permits rollback instead. Finish other fallible preparation earlier.
  //
  // Publication, not io_uring_enter's submitted count, is the boundary: short
  // submit, zero progress, EINTR and other submit errors NEVER permit rollback
  // or republishing an already enqueued operation. Proven transport withdrawal
  // is a separate explicit transition below.
  LIBC_INLINE bool mark_published(Credit &credit) {
    if (credit.owner != this || credit.published)
      return false;
    credit.published = true;
    return true;
  }

  // Only for a reservation whose SQE has never become kernel-visible. Its
  // token stays burned. The empty holder may be reserved again with a new one.
  LIBC_INLINE bool rollback(Credit &credit) {
    if (credit.owner != this || credit.published)
      return false;
    release(credit);
    return true;
  }

  // Call only AFTER poll/wait has actually removed the matching CQE from the
  // ring, not on readiness, result retrieval or cancellation success. Validate
  // the CQE's single-shot contract before calling. Wrong-owner, wrong-token,
  // unpublished, stale and duplicate events leave all accounting unchanged.
  // This releases CQ capacity only; it neither completes nor reclaims a
  // request.
  LIBC_INLINE bool consume(Credit &credit, uint64_t token) {
    if (credit.owner != this || !credit.published ||
        token != credit.token_value)
      return false;
    release(credit);
    return true;
  }

  // Only after the exclusive transport owner has removed this exact token
  // from the unconsumed SQ tail. This is NOT a completion or a cancellation
  // result. Enter errors alone never authorize this transition. Tokens remain
  // burned and no CQE may subsequently be accepted for the withdrawn entry.
  LIBC_INLINE bool withdraw(Credit &credit, uint64_t token) {
    if (credit.owner != this || !credit.published ||
        token != credit.token_value)
      return false;
    release(credit);
    return true;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_COMPLETION_BUDGET_H
