//===-- Private Linux AIO completion routing -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_COMPLETION_QUEUE_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_COMPLETION_QUEUE_H

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/OSUtil/linux/io_uring.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/aio_completion_budget.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

// One exclusive ring owner, serialized by the caller's service lock, including
// queries, destruction and prepare/enqueue/publish. Suppress cancellation and
// application signal handlers throughout that transaction. RingLike supplies
// Ring::poll and its independent, no-syscall check_integrity contract.
// The ring and watermark outlive this stable owner. Never reset the ring with
// outstanding entries. All CQE producers must use this queue, single-shot only.
//
// The caller retains one nondecreasing watermark for this token namespace
// across drained restarts; no concurrent owners may share it or the ring.
// Independent rings may have equal tokens: route by ring identity first.
template <class RingLike = io_uring::Ring<>> class AioCompletionQueue {
  [[noreturn]] static void fatal() {
    (void)LIBC_NAMESPACE::syscall_impl<long>(SYS_exit_group, 127);
    __builtin_trap();
  }

public:
  enum class Kind { Transfer, Metadata, Sync, Cancel };

  class Entry {
    friend class AioCompletionQueue;
    AioCompletionBudget::Credit credit;
    AioCompletionQueue *owner = nullptr;
    Entry *next = nullptr;
    Kind kind = Kind::Transfer;
    void *context = nullptr;
    uint32_t max_result = 0;
    bool published = false;

  public:
    LIBC_INLINE Entry() = default;
    Entry(const Entry &) = delete;
    Entry &operator=(const Entry &) = delete;
    LIBC_INLINE ~Entry() {
      if (owner)
        fatal();
    }
    LIBC_INLINE uint64_t token() const { return credit.token(); }
    LIBC_INLINE bool reclaimable() const { return owner == nullptr; }
  };

  struct Completion {
    Kind kind;
    void *context;
    uint64_t token;
    int32_t raw_result;
  };

private:
  RingLike &ring;
  uint64_t &watermark;
  AioCompletionBudget budget;
  Entry *head = nullptr;

  LIBC_INLINE void detach(Entry **link) {
    Entry &entry = **link;
    *link = entry.next;
    entry.next = nullptr;
    entry.owner = nullptr;
    entry.context = nullptr;
    entry.published = false;
  }

public:
  LIBC_INLINE AioCompletionQueue(RingLike &ring, uint64_t &watermark)
      : ring(ring), watermark(watermark), budget(watermark) {}
  AioCompletionQueue(const AioCompletionQueue &) = delete;
  AioCompletionQueue &operator=(const AioCompletionQueue &) = delete;
  LIBC_INLINE ~AioCompletionQueue() {
    if (budget.outstanding())
      fatal();
  }

  // Configure once on an empty, initialized ring. Caller proves capacity <=
  // actual CQ size; successful Ring::initialize(n) makes n a safe bound.
  // This does not initialize/reset the ring or infer its negotiated geometry.
  LIBC_INLINE ErrorOr<void> initialize(uint32_t capacity) {
    if (budget.capacity())
      return Error(EBUSY);
    auto health = ring.check_integrity();
    if (!health)
      return Error(health.error());
    return budget.initialize(capacity);
  }

  LIBC_INLINE uint32_t outstanding() const { return budget.outstanding(); }
  LIBC_INLINE uint32_t available() const { return budget.available(); }

  // Complete fallible preparation before enqueue. Use the returned token as
  // SQE user_data, never an application aiocb pointer. Keep Entry at a stable
  // address until rollback or completion. Reserve separate cancel credits;
  // callers must leave control headroom if cancellation must work at
  // saturation.
  LIBC_INLINE ErrorOr<uint64_t> prepare(Entry &entry, Kind kind, void *context,
                                        uint32_t max_success_result) {
    if (!entry.reclaimable() || max_success_result > INT32_MAX ||
        (kind != Kind::Transfer && kind != Kind::Metadata &&
         kind != Kind::Sync && kind != Kind::Cancel) ||
        (kind != Kind::Transfer && max_success_result != 0))
      return Error(EINVAL);
    auto token = budget.reserve(entry.credit);
    if (!token)
      return Error(token.error());
    watermark = budget.last_issued_token();
    entry.owner = this;
    entry.kind = kind;
    entry.context = context;
    entry.max_result = max_success_result;
    entry.next = head;
    head = &entry;
    return *token;
  }

  // Must follow successful enqueue under the SAME lock, before any poll.
  // Submit errors/short submits never authorize rollback or republishing.
  LIBC_INLINE void publish(Entry &entry) {
    if (entry.owner != this || !budget.mark_published(entry.credit))
      fatal();
    entry.published = true;
  }

  // Only after enqueue failed or was never attempted. Tokens remain burned.
  LIBC_INLINE bool rollback(Entry &entry) {
    if (entry.owner != this || entry.published)
      return false;
    Entry **link = &head;
    while (*link && *link != &entry)
      link = &(*link)->next;
    if (!*link || !budget.rollback(entry.credit))
      fatal();
    detach(link);
    return true;
  }

  // Recovery after an enter failure, never a substitute for polling consumed
  // requests. RingLike must implement Ring::withdraw_last under exclusive
  // access to ALL ring operations. Removal and credit retirement form one
  // transaction with cancellation/application handlers suppressed. The caller
  // still owns the request and must publish its explicit failure separately.
  LIBC_INLINE ErrorOr<void> withdraw(Entry &entry) {
    if (entry.owner != this || !entry.published)
      return Error(EINVAL);
    auto health = ring.check_integrity();
    if (!health)
      fatal();
    auto result = ring.withdraw_last(entry.token());
    if (!result) {
      // EAGAIN: already consumed/empty. EINVAL: not the last pending token.
      // Neither releases ownership. Other errors violate the trusted ring
      // contract after the successful integrity check.
      if (result.error() != EAGAIN && result.error() != EINVAL)
        fatal();
      return Error(result.error());
    }
    Entry **link = &head;
    while (*link && *link != &entry)
      link = &(*link)->next;
    if (!*link || !budget.withdraw(entry.credit, entry.token()))
      fatal();
    detach(link);
    return {};
  }

  // No callbacks or request-state transitions. Even after detachment the caller
  // must retain the referenced request, buffers and other storage through
  // processing of Completion. An error CQE is a normal terminal completion.
  LIBC_INLINE ErrorOr<Completion> poll() {
    io_uring_cqe cqe{};
    auto result = ring.poll(cqe);
    auto health = ring.check_integrity();
    if (!health) {
      if (budget.outstanding() || result)
        fatal();
      return Error(health.error());
    }
    if (!result)
      return Error(result.error());
    Entry **link = &head;
    while (*link && (*link)->token() != cqe.user_data)
      link = &(*link)->next;
    if (!cqe.user_data || !*link)
      fatal();
    Entry &entry = **link;
    if (entry.owner != this || !entry.published || cqe.flags != 0 ||
        (cqe.res >= 0 && uint32_t(cqe.res) > entry.max_result) ||
        cqe.res < -4095 || cqe.res == -EINPROGRESS)
      fatal();
    Completion completion{entry.kind, entry.context, entry.token(), cqe.res};
    if (!budget.consume(entry.credit, cqe.user_data))
      fatal();
    detach(link);
    return completion;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_COMPLETION_QUEUE_H
