//===-- Private AIO same-file dependencies ---------------------*- C++ -*-===//
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

// Caller-serialized, bounded metadata decisions, not an AIO provider. All
// access requires the same service lock. No allocation, syscalls, I/O waiting,
// CQ accounting or AioRequest ownership is performed here.
//
// Admit every relevant accepted operation (including syncs) in admission order,
// before publishing either its original I/O or asynchronous identity lookup.
// Descriptor numbers/open-description addresses are NOT file identities. The
// caller must retain the captured descriptor and bind lookup results to that
// descriptor, this owner, process and order; numeric order is not
// authentication. A cancel/control CQE is not the original operation's terminal
// event.
//
// A sync owns a snapshot of all retained earlier records until decide() returns
// Ready or an error. Later admissions never enter that snapshot. release()
// prevents early removal, including of terminal records, while any later sync
// still owns a snapshot. Keep records and this owner alive at stable addresses;
// destruction does not release ownership. Separately retain any AioRequest
// references, lookup output buffers and captured descriptors until their users
// drain. In particular, original completion does not finish a pending lookup.
//
// Records completed and released BEFORE a sync's admission need not be in its
// snapshot: they have no remaining ordering dependency. Errors of operations
// pending at sync admission are retained until its decision. This component
// says nothing about persistence or eventual kernel sync success.
class AioFileDependencies {
public:
  enum class Kind { Operation, Sync };
  enum class FileType {
    Regular,
    Directory,
    Block,
    Character,
    Fifo,
    Socket,
    Symlink
  };
  enum class IdentityState { Pending, Known, Unknown };
  enum class Decision { Wait, Ready };

  // Full tuple comparison, never a hash. Type is the verified, normalized file
  // type, not permission bits or a catch-all nonregular category. Whether a
  // particular descriptor supports the requested sync is checked separately
  // by the integrating service/kernel, not by this dependency component.
  struct Identity {
    uint32_t device_major;
    uint32_t device_minor;
    uint64_t inode;
    FileType type;
  };

  class Record {
    friend class AioFileDependencies;
    AioFileDependencies *owner = nullptr;
    Record *next = nullptr;
    uint64_t order_value = 0;
    Kind kind = Kind::Operation;
    IdentityState identity_state = IdentityState::Pending;
    Identity identity = {};
    int identity_error = 0;
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
    LIBC_INLINE IdentityState classification() const { return identity_state; }
    // For the current admission: Ready or a metadata error consumes the sync
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

  LIBC_INLINE static bool valid_type(FileType type) {
    switch (type) {
    case FileType::Regular:
    case FileType::Directory:
    case FileType::Block:
    case FileType::Character:
    case FileType::Fifo:
    case FileType::Socket:
    case FileType::Symlink:
      return true;
    }
    return false;
  }

  LIBC_INLINE static bool same_file(const Identity &left,
                                    const Identity &right) {
    return left.type == right.type && left.device_major == right.device_major &&
           left.device_minor == right.device_minor && left.inode == right.inode;
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
  // terminal, but outstanding identity lookup ownership must still drain.
  LIBC_INLINE ErrorOr<uint64_t> admit(Record &record, Kind kind) {
    if (!record.reclaimable() ||
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
    record.identity_state = IdentityState::Pending;
    record.identity_error = 0;
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

  // Call only with verified metadata for the captured descriptor. For EVERY
  // type, the caller must qualify that (dev major, dev minor, inode, type) is a
  // stable, unique identity across the compared filesystems/namespaces for the
  // retained descriptors, including dup aliases and independent opens.
  // STATX success or field presence alone does not establish this property on
  // every filesystem. Missing fields, unqualified identities and lookup
  // failures must use set_unknown(), not an invented key. No inode/dev value
  // (including zero) is reserved as an unknown sentinel.
  LIBC_INLINE bool set_known(Record &record, uint64_t order,
                             Identity identity) {
    if (!matches(record, order) ||
        record.identity_state != IdentityState::Pending ||
        !valid_type(identity.type))
      return false;
    record.identity = identity;
    record.identity_state = IdentityState::Known;
    return true;
  }

  // Preserve a positive terminal metadata errno; EOPNOTSUPP is suitable for an
  // otherwise successful lookup whose identity is unqualified, for ANY type.
  LIBC_INLINE bool set_unknown(Record &record, uint64_t order, int error) {
    if (!matches(record, order) ||
        record.identity_state != IdentityState::Pending || error <= 0 ||
        error == EINPROGRESS)
      return false;
    record.identity_error = error;
    record.identity_state = IdentityState::Unknown;
    return true;
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
  // Ready authorizes sync submission, NEVER a claim of synchronization. Errors
  // from unknown identities must fail the sync, not be treated as Ready.
  // Invalid calls return EINVAL without changing the snapshot; a valid call
  // can also return a final metadata EINVAL supplied through set_unknown().
  // decision_final() exposes that distinction for a previously undecided sync.
  LIBC_INLINE ErrorOr<Decision> decide(Record &sync, uint64_t order) {
    if (!matches(sync, order) || sync.kind != Kind::Sync || sync.decided)
      return Error(EINVAL);

    if (sync.identity_state == IdentityState::Unknown) {
      sync.decided = true;
      return Error(sync.identity_error);
    }
    // Unknown identities fail immediately. Successful terminal predecessors,
    // and failures already terminal before this sync was admitted, need no
    // classification. Other failures must still be associated with this file.
    bool waiting = sync.identity_state == IdentityState::Pending;
    int prior_error = 0;
    for (Record *record = head; record != &sync; record = record->next) {
      if (record->terminal &&
          (!record->terminal_error || record->terminal_order < sync.order_value))
        continue;
      if (record->identity_state == IdentityState::Unknown) {
        sync.decided = true;
        return Error(record->identity_error);
      }
      if (record->identity_state == IdentityState::Pending) {
        waiting = true;
      } else if (sync.identity_state == IdentityState::Known &&
                 same_file(record->identity, sync.identity)) {
        if (!record->terminal)
          waiting = true;
        else if (!prior_error)
          prior_error = record->terminal_error;
      }
    }
    if (waiting)
      return Decision::Wait;
    sync.decided = true;
    if (prior_error)
      return Error(prior_error);
    return Decision::Ready;
  }

  // Snapshot ownership outlives original completion. Even known-different
  // records are retained until every later undecided sync has decided. A
  // pending identity lookup separately prevents reclamation after I/O ends.
  // Successful release allows reuse of this storage, with a NEW order only.
  LIBC_INLINE bool release(Record &record, uint64_t order) {
    if (!matches(record, order) || !record.terminal ||
        record.identity_state == IdentityState::Pending)
      return false;
    for (Record *later = record.next; later != nullptr; later = later->next)
      if (later->kind == Kind::Sync && !later->decided)
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
