//===-- Private AIO original-fd dependencies -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_FILE_DEPENDENCIES_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_FILE_DEPENDENCIES_H

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Caller-serialized, bounded dependency decisions, not an AIO provider. All
// access requires the same service lock. No allocation, syscalls, I/O waiting,
// CQ accounting or AioRequest ownership is performed here.
//
// Admit every relevant accepted operation (including syncs) in admission order,
// before publishing its original I/O. Groups use the original numeric fd at
// admission, never a private duplicate, file identity or open description.
// Different numeric aliases do not group; reuse of the same numeric fd does.
// A cancel/control CQE is not the original operation's terminal event.
//
// A sync owns a snapshot of retained earlier records with the same original fd
// until decide() returns Ready or an error. Later admissions never enter that
// snapshot. release() prevents early removal, including of terminal records,
// while a later same-fd sync still owns a snapshot. Unrelated syncs do not
// retain records. Keep records and this owner alive at stable addresses;
// destruction does not release ownership. Separately retain any AioRequest
// references and captured descriptors until their users drain.
//
// Records completed and released BEFORE a sync's admission need not be in its
// snapshot: they have no remaining ordering dependency. Errors of operations
// pending at sync admission are retained until its decision. This component
// says nothing about persistence or eventual kernel sync success.
class AioFileDependencies {
public:
  enum class Kind { Operation, Sync };
  enum class Decision { Wait, Ready };

  class Record {
    friend class AioFileDependencies;
    AioFileDependencies *owner = nullptr;
    Record *next = nullptr;
    uint64_t order_value = 0;
    Kind kind = Kind::Operation;
    int original_fd = -1;
    int terminal_error = 0;
    uint64_t terminal_order = 0;
    bool terminal = false;
    bool decided = false;

  public:
    LIBC_INLINE constexpr Record() = default;
    Record(const Record &) = delete;
    Record &operator=(const Record &) = delete;

    LIBC_INLINE uint64_t order() const { return order_value; }
    LIBC_INLINE bool reclaimable() const { return owner == nullptr; }
    // For the current admission: Ready or a predecessor error consumes the sync
    // decision; Wait or an invalid call does not. Admission resets this flag.
    LIBC_INLINE bool decision_final() const { return decided; }
  };

private:
  const uint32_t limit;
  uint32_t held = 0;
  uint64_t last_order;
  Record *head = nullptr;
  Record *tail = nullptr;

  LIBC_INLINE bool matches(const Record &record, uint64_t order) const {
    return record.owner == this && order != 0 && record.order_value == order;
  }

public:
  // A zero capacity admits nothing. Preserve the watermark across drained
  // replacements in the same order namespace; never lower it or wrap/reuse an
  // issued order. Capacity counts retained records, not just in-flight I/O.
  LIBC_INLINE explicit constexpr AioFileDependencies(
      uint32_t capacity, uint64_t last_issued_order = 0)
      : limit(capacity), last_order(last_issued_order) {}
  AioFileDependencies(const AioFileDependencies &) = delete;
  AioFileDependencies &operator=(const AioFileDependencies &) = delete;

  LIBC_INLINE uint32_t outstanding() const { return held; }
  LIBC_INLINE uint64_t last_issued_order() const { return last_order; }

  // Establish the admission linearization point under the caller's lock.
  // Failure changes neither the record nor the watermark. There is no rollback
  // of an accepted order. Rejected-before-publication work may be marked
  // terminal once nonpublication is confirmed.
  LIBC_INLINE ErrorOr<uint64_t> admit(Record &record, Kind kind,
                                      int original_fd) {
    if (!record.reclaimable() || original_fd < 0 ||
        (kind != Kind::Operation && kind != Kind::Sync))
      return Error(EINVAL);
    if (last_order == UINT64_MAX)
      return Error(EOVERFLOW);
    if (held == limit)
      return Error(EAGAIN);
    record.owner = this;
    record.next = nullptr;
    record.order_value = ++last_order;
    record.kind = kind;
    record.original_fd = original_fd;
    record.terminal_error = 0;
    record.terminal_order = 0;
    record.terminal = false;
    record.decided = false;
    if (tail != nullptr)
      tail->next = &record;
    else
      head = &record;
    tail = &record;
    ++held;
    return record.order_value;
  }

  // Only original-operation terminal completion (success OR failure), or
  // confirmed nonpublication, removes the ordering dependency. Syncs must first
  // have a final decision; after a decision error the caller fails that sync
  // explicitly and calls complete(), without publishing a successful sync.
  LIBC_INLINE bool complete(Record &record, uint64_t order, int error = 0) {
    if (!matches(record, order) || record.terminal || error < 0 ||
        error == EINPROGRESS ||
        (record.kind == Kind::Sync && !record.decided))
      return false;
    record.terminal_error = error;
    record.terminal_order = last_order;
    record.terminal = true;
    return true;
  }

  // Wait is retryable; Ready and errors consume the snapshot exactly once.
  // Ready authorizes sync submission, NEVER a claim of synchronization. The
  // first admission-order error among same-fd predecessors pending at sync
  // admission wins, after all those predecessors complete.
  // Invalid calls return EINVAL without changing the snapshot; a valid call
  // can also return a final predecessor EINVAL supplied through complete().
  // decision_final() exposes that distinction for a previously undecided sync.
  LIBC_INLINE ErrorOr<Decision> decide(Record &sync, uint64_t order) {
    if (!matches(sync, order) || sync.kind != Kind::Sync || sync.decided)
      return Error(EINVAL);

    bool waiting = false;
    int prior_error = 0;
    for (Record *record = head; record != &sync; record = record->next) {
      if (record->original_fd != sync.original_fd)
        continue;
      if (record->terminal &&
          (!record->terminal_error || record->terminal_order < sync.order_value))
        continue;
      if (!record->terminal)
        waiting = true;
      else if (!prior_error)
        prior_error = record->terminal_error;
    }
    if (waiting)
      return Decision::Wait;
    sync.decided = true;
    if (prior_error)
      return Error(prior_error);
    return Decision::Ready;
  }

  // End a queued sync without issuing it (for example, local cancellation).
  // This releases its predecessor snapshot, not its SQE ownership.
  // The caller proves that no original sync has been published.
  LIBC_INLINE bool abandon_sync(Record &sync, uint64_t order, int error) {
    if (!matches(sync, order) || sync.kind != Kind::Sync || sync.terminal ||
        error <= 0 || error == EINPROGRESS)
      return false;
    sync.decided = true;
    return complete(sync, order, error);
  }

  // Snapshot ownership outlives original completion. Records are retained
  // until every later undecided same-fd sync has decided.
  // Successful release allows reuse of this storage, with a NEW order only.
  LIBC_INLINE bool release(Record &record, uint64_t order) {
    if (!matches(record, order) || !record.terminal)
      return false;
    for (Record *later = record.next; later != nullptr; later = later->next)
      if (later->kind == Kind::Sync && !later->decided &&
          later->original_fd == record.original_fd)
        return false;
    Record *previous = nullptr;
    for (Record *current = head; current != &record; current = current->next)
      previous = current;
    if (previous != nullptr)
      previous->next = record.next;
    else
      head = record.next;
    if (tail == &record)
      tail = previous;
    record.owner = nullptr;
    record.next = nullptr;
    record.order_value = 0;
    --held;
    return true;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_FILE_DEPENDENCIES_H
