//===-- Resident runtime symbol scopes ---------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_SCOPES_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_SCOPES_H

#include "loader/linux/image_registry.h"
#include "loader/linux/snapshot_storage.h"
#include "loader/linux/symbols.h"
#include "loader/linux/unwind_images.h"
#include "loader/linux/unwind_leases.h"
#include "src/__support/CPP/atomic.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

class RuntimeScopes {
public:
  using Image = ImageRegistry::Image;
  struct Value {
    SymbolValue symbol;
    const Image *image = nullptr;
  };
  struct Scope {
    cpp::span<const Image *const> images;
    cpp::span<const SymbolObject> objects;
    StartupSymbols symbols;
    bool contains(const Image *) const;
    SymbolError lookup(Bytes name, Value &) const;
  };
  struct Handle {
    uint64_t token;
    const Scope *local;
    bool global_namespace;
  };
  struct Context {
    const Image *image;
    const Scope *local, *binding;
  };
  struct UnwindState {
    const ImageRegistry::Snapshot *registry;
    StartupUnwindImages images;
  };
  struct Snapshot {
    const ImageRegistry::Snapshot *registry;
    Scope global;
    cpp::span<const Handle> handles;
    cpp::span<const Context> contexts;
    const UnwindState *unwind = nullptr;
    bool owns_registry = false;
    const Context *context(const Image *) const;
  };
  class Transaction {
    friend class RuntimeScopes;
    Arena arena;
    RuntimeScopes *owner = nullptr;
    SnapshotStorage<Snapshot>::Batch *batch = nullptr;
    const Snapshot *base = nullptr;
    Snapshot *candidate = nullptr;
    bool active = false, published = false;
    bool unwind_required = false;
    UnwindLeases<Snapshot>::Reservation unwind_reservation;
    cpp::span<const Image *const> retiring{};

  public:
    explicit Transaction(size_t page) : arena(page), unwind_reservation(page) {}
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    const Snapshot *view() const { return published ? nullptr : candidate; }
    long abort();
  };

private:
  SnapshotStorage<Snapshot> storage;
  RegistrySequence tokens;
  long begin(Transaction &);
  static long make_scope(Arena &, cpp::span<const Image *const>, Scope &);
  static long closure(Arena &, const ImageRegistry::Snapshot &, const Image &,
                      Scope &);
  static long combine(Arena &, const Scope &, const Scope &, Scope &);
  static long copy_scopes(Transaction &);

public:
  UnwindLeases<Snapshot> unwind_leases;
  explicit RuntimeScopes(size_t page) : storage(page), unwind_leases(page) {}
  RuntimeScopes(const RuntimeScopes &) = delete;
  RuntimeScopes &operator=(const RuntimeScopes &) = delete;
  // Writers hold mutation serialization and cancellation suppression. Inputs
  // are admitted immutable image records; metadata and ELF bytes stay resident.
  long prepare_startup(const ImageRegistry::Snapshot &, Transaction &);
  // A null root denotes dlopen(NULL); it sees the current global namespace.
  long prepare_open(const ImageRegistry::Snapshot &, const Image *root,
                    bool global, Transaction &, uint64_t &token);
  long prepare_close(uint64_t token, Transaction &);
  // Private coordinator operation, not public dlclose. Reject live handles,
  // rooted owners and incoming dependency/binding edges. Does not unmap.
  long prepare_retire(cpp::span<const Image *const>, Transaction &);
  // After relocation/protection, before publication or any user callback.
  // Implemented by runtime_unwind; failed preparation stays private to the tx.
  long prepare_unwind(Transaction &);
  // The coordinator must finish relocation/TLS/protection before publication.
  // This publishes scopes only, not registry records or constructor completion.
  long publish(Transaction &);
  bool can_publish(const Transaction &) const;
  const Snapshot *snapshot() const { return storage.snapshot(); }
  using Lease = SnapshotStorage<Snapshot>::Lease;
  Lease acquire() const { return Lease(storage); }
  // Writer safe point after readers drain. EBUSY is not permission to unmap.
  long collect_snapshots() { return storage.collect(); }
  size_t active_readers() const { return storage.active_readers(); }
  bool readers_drained(const Image *image) const {
    return !storage.referenced([&](const Snapshot &view) {
      for (const Image *entry : view.registry->images)
        if (entry == image)
          return true;
      return false;
    });
  }
  bool owns_unwind_token(uintptr_t token) const;

  // Untrusted handles are integer tokens, never dereferenced application data.
  static SymbolError lookup(const Snapshot &, uint64_t token, Bytes, Value &);
  static SymbolError lookup_default(const Snapshot &, const Image *caller,
                                    Bytes, Value &);
  static SymbolError lookup_next(const Snapshot &, const Image *caller, Bytes,
                                 Value &);
  static SymbolError resolve_binding(const Snapshot &, const Image *requester,
                                     Address index, Value &);
  // Preserve the original API caller before entering helpers. Only an admitted
  // executable LOAD identifies a caller; unknown/ambiguous PCs are rejected.
  static SymbolError caller(const Snapshot &, Address pc, const Image *&);
  // snapshot() is a writer-serialized borrow. Concurrent or retained reads
  // require a Lease. Image bytes still await coordinated physical retirement.
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
