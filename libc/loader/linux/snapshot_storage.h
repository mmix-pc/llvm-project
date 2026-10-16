//===-- Snapshot reader storage ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_SNAPSHOT_STORAGE_H
#define LLVM_LIBC_LOADER_LINUX_SNAPSHOT_STORAGE_H

#include "hdr/errno_macros.h"
#include "loader/linux/arena.h"
#include "loader/linux/retirement_events.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Writers serialize reserve/publish/collect. Readers enter before loading the
// current pointer. Sequential consistency makes a reader either retain the old
// batch or observe the replacement before the writer can recycle old storage.
// Readers never allocate or take loader locks. An optional event sink may issue
// a nonblocking wake, but collection remains an explicit writer safe point,
// never work performed during unwind/cancellation.
template <class T> class SnapshotStorage {
public:
  struct Batch {
    Arena arena;
    Batch *next;
    const T *view = nullptr;
    mutable cpp::Atomic<bool> pinned{false};
    cpp::Atomic<size_t> readers{0};
    bool reserved = false, dirty = false;
    RetirementEvents *events;
    Batch(size_t page, Batch *next, RetirementEvents *events)
        : arena(page), next(next), events(events) {}
    void release() {
      if (readers.fetch_sub(1) == 1 && events)
        events->request();
    }
  };

private:
  size_t page;
  RetirementEvents *events;
  Arena controls;
  mutable cpp::Atomic<Batch *> batches{nullptr};
  mutable cpp::Atomic<Batch *> current{nullptr};
  mutable cpp::Atomic<size_t> readers{0};

public:
  explicit SnapshotStorage(size_t page, RetirementEvents *events = nullptr)
      : page(page), events(events), controls(page) {}
  void retain() const {
    size_t count = readers.load();
    do {
      if (count == __SIZE_MAX__)
        __builtin_trap();
    } while (!readers.compare_exchange_weak(count, count + 1));
  }
  void release() const { readers.fetch_sub(1); }
  class Lease {
    Batch *batch = nullptr;

  public:
    Lease() = default;
    explicit Lease(const SnapshotStorage &storage) {
      storage.retain();
      batch = storage.current.load();
      if (batch) {
        size_t count = batch->readers.load();
        do {
          if (count == __SIZE_MAX__)
            __builtin_trap();
        } while (!batch->readers.compare_exchange_weak(count, count + 1));
      }
      storage.release();
      if (storage.events)
        storage.events->request();
    }
    ~Lease() {
      if (batch)
        batch->release();
    }
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;
    Lease(Lease &&other) : batch(other.batch) {
      other.batch = nullptr;
    }
    Lease &operator=(Lease &&other) {
      if (this != &other) {
        if (batch)
          batch->release();
        batch = other.batch;
        other.batch = nullptr;
      }
      return *this;
    }
    const T *get() const { return batch ? batch->view : nullptr; }
    // Unobservable copies of public unwind contexts cannot release an exact
    // owner yet. Explicitly pin their metadata instead of guessing a lifetime.
    void pin() const {
      if (batch)
        batch->pinned.store(true);
    }
    // Transfer to a cleanup owner outside the bootstrap loader's no-EH frame.
    void *detach() {
      Batch *result = batch;
      batch = nullptr;
      return result;
    }
    static void release_detached(void *token) {
      if (token)
        static_cast<Batch *>(token)->release();
    }
    // The caller already owns a reference, so the batch cannot be recycled.
    static void retain_detached(void *token) {
      auto *batch = static_cast<Batch *>(token);
      size_t count = batch->readers.load();
      do {
        if (!count || count == __SIZE_MAX__)
          __builtin_trap();
      } while (!batch->readers.compare_exchange_weak(count, count + 1));
    }
    static const T *view_detached(void *token) {
      return static_cast<Batch *>(token)->view;
    }
    static void pin_detached(void *token) {
      static_cast<Batch *>(token)->pinned.store(true);
    }
  };

  const T *snapshot() const {
    auto *batch = current.load();
    return batch ? batch->view : nullptr;
  }
  size_t active_readers() const {
    size_t count = readers.load();
    for (Batch *batch = batches.load(); batch; batch = batch->next)
      count += batch->readers.load();
    return count;
  }
  long collect() {
    if (readers.load())
      return -EBUSY;
    long error = 0;
    bool busy = false;
    for (Batch *batch = batches.load(); batch; batch = batch->next) {
      if (batch == current.load() || batch->reserved || batch->pinned.load())
        continue;
      if (batch->readers.load()) {
        busy = true;
        continue;
      }
      batch->view = nullptr;
      long result = batch->arena.reset();
      batch->dirty = result != 0;
      if (!error)
        error = result;
    }
    return error ? error : busy ? -EBUSY : 0;
  }
  long reserve(Batch *&out) {
    out = nullptr;
    for (Batch *batch = batches.load(); batch; batch = batch->next)
      if (!batch->view && !batch->reserved && !batch->dirty) {
        batch->reserved = true;
        out = batch;
        return 0;
      }
    void *memory;
    long error = controls.allocate(sizeof(Batch), alignof(Batch), memory);
    if (error)
      return error;
    out = new (memory) Batch(page, batches.load(), events);
    out->reserved = true;
    batches.store(out);
    return 0;
  }
  void cancel(Batch *batch) {
    if (batch)
      batch->reserved = false;
  }
  bool can_publish(Batch *batch, const Arena &arena) const {
    return batch && batch->reserved && batch->arena.can_adopt(arena);
  }
  bool publish(Batch *batch, Arena &arena, const T *view) {
    if (!view || !can_publish(batch, arena) || !batch->arena.adopt(arena))
      return false;
    batch->view = view;
    batch->reserved = false;
    current.store(batch);
    return true;
  }
  // Hold a lease while traversing. Pinned batches are immutable forever.
  template <class Predicate> bool pinned_contains(Predicate predicate) const {
    for (const Batch *batch = batches.load(); batch; batch = batch->next)
      if (batch->pinned.load() && predicate(*batch->view))
        return true;
    return false;
  }
  // Writer-serialized retirement barrier. An entering reader conservatively
  // blocks reclamation; otherwise only the generations it actually holds do.
  template <class Predicate> bool referenced(Predicate predicate) const {
    if (readers.load())
      return true;
    for (Batch *batch = batches.load(); batch; batch = batch->next)
      if (batch->view &&
          (batch == current.load() || batch->readers.load() ||
           batch->pinned.load()) &&
          predicate(*batch->view))
        return true;
    return false;
  }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
