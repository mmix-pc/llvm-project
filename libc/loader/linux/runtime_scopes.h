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
#include "loader/linux/symbols.h"
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
  struct Snapshot {
    const ImageRegistry::Snapshot *registry;
    Scope global;
    cpp::span<const Handle> handles;
    cpp::span<const Context> contexts;
    const Context *context(const Image *) const;
  };
  class Transaction {
    friend class RuntimeScopes;
    Arena arena;
    const RuntimeScopes *owner = nullptr;
    const Snapshot *base = nullptr;
    Snapshot *candidate = nullptr;
    bool active = false, published = false;

  public:
    explicit Transaction(size_t page) : arena(page) {}
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    const Snapshot *view() const { return candidate; }
    long abort();
  };

private:
  Arena resident;
  RegistrySequence tokens;
  mutable cpp::Atomic<const Snapshot *> current{nullptr};
  long begin(Transaction &);
  static long make_scope(Arena &, cpp::span<const Image *const>, Scope &);
  static long closure(Arena &, const ImageRegistry::Snapshot &, const Image &,
                      Scope &);
  static long combine(Arena &, const Scope &, const Scope &, Scope &);

public:
  explicit RuntimeScopes(size_t page) : resident(page) {}
  RuntimeScopes(const RuntimeScopes &) = delete;
  RuntimeScopes &operator=(const RuntimeScopes &) = delete;
  // Writers hold mutation serialization and cancellation suppression. Inputs
  // are admitted immutable image records; metadata and ELF bytes stay resident.
  long prepare_startup(const ImageRegistry::Snapshot &, Transaction &);
  // A null root denotes dlopen(NULL); it sees the current global namespace.
  long prepare_open(const ImageRegistry::Snapshot &, const Image *root,
                    bool global, Transaction &, uint64_t &token);
  long prepare_close(uint64_t token, Transaction &);
  // The coordinator must finish relocation/TLS/protection before publication.
  // This publishes scopes only, not registry records or constructor completion.
  long publish(Transaction &);
  const Snapshot *snapshot() const {
    return current.load(cpp::MemoryOrder::ACQUIRE);
  }

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
  // FIXME: Closed tokens disappear from new snapshots, but image scopes and
  // old reader snapshots remain resident until coordinated unloading exists.
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
