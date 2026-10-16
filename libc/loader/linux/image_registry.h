//===-- Resident image registry ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_IMAGE_REGISTRY_H
#define LLVM_LIBC_LOADER_LINUX_IMAGE_REGISTRY_H

#include "loader/linux/arena.h"
#include "loader/linux/io.h"
#include "loader/linux/module_ownership.h"
#include "loader/linux/tls_layout.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Zero is reserved. Exhaustion never wraps into an earlier identity.
class RegistrySequence {
  uint64_t next = 1;

public:
  bool can_reserve(uint64_t count) const {
    return next && count && count - 1 <= ~uint64_t(0) - next;
  }
  bool reserve(uint64_t count, uint64_t &first);
};

class ImageRegistry {
public:
  struct Image;
  struct Binding {
    const Image *provider;
    const Binding *next;
  };
  struct Image {
    ElfImage elf;
    StartupTLSModule tls;
    bool startup;
    mutable ModuleOwnership ownership{tls.module_id, tls.generation, startup};
    bool discovered = false, identity_known = false;
    FileIdentity identity{};
    Bytes path{};
    const Image *parent = nullptr;
    cpp::span<const Image *const> dependencies{};
    // Written only in the private transaction, before relocation completes.
    mutable const Binding *bindings = nullptr;
  };
  // Indices refer to the complete candidate snapshot (old images first).
  // Parent is the first-discovery ancestor, never a dependency back-edge.
  struct Discovery {
    bool identity_known = false;
    FileIdentity identity{};
    Bytes path{};
    size_t parent = ~size_t(0);
    cpp::span<const size_t> dependencies{};
  };
  struct Snapshot {
    uint64_t generation;
    cpp::span<const Image *const> images;
    const Image *find(uint64_t id, uint64_t module_generation) const;
    // Caller holds mutation serialization. Explicit roots supplement permanent
    // roots and external ownership tickets. Edges are not reference-counted:
    // unreachable cycles belong to the returned set together. Does not retire
    // or unmap; storage belongs to scratch and must not escape its lifetime.
    long reclaimable(Arena &scratch, cpp::span<const Image *const> roots,
                     cpp::span<const Image *const> &out) const;
    // Prepare an independent discovery view. Descriptor ownership and the
    // admission transition stay with the serialized publication coordinator.
    long without(Arena &, cpp::span<const Image *const> removed,
                 Snapshot *&out) const;
  };
  class Transaction {
    friend class ImageRegistry;
    enum class State {
      Empty,
      Prepared,
      Relocated,
      TLSReady,
      Protected,
      Published,
      Aborting
    };
    Arena arena;
    State state = State::Empty;
    const ImageRegistry *owner = nullptr;
    const Snapshot *base = nullptr;
    Snapshot *candidate = nullptr;

  public:
    explicit Transaction(size_t page_size) : arena(page_size) {}
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    const Snapshot *view() const;
    // The transaction coordinator attests completed operations, in order.
    // These methods do not themselves relocate, allocate TLS or mprotect.
    bool mark_relocated();
    bool mark_tls_ready();
    bool mark_protected();
    // Explicit lifetime: abort unpublished storage before destroying this
    // object. Published storage is process-owned and cannot be reset here. On
    // failed cleanup, retain the arena and refuse reuse until another abort
    // succeeds.
    long abort();
  };

private:
  Arena resident;
  RegistrySequence ids, generations;
  const Snapshot *current = nullptr;

public:
  explicit ImageRegistry(size_t page_size) : resident(page_size) {}
  ImageRegistry(const ImageRegistry &) = delete;
  ImageRegistry &operator=(const ImageRegistry &) = delete;
  // Caller serializes ALL access. Concurrent publication is not provided by
  // this component. ELF byte views and mappings must outlive published records.
  // The first batch is main-first startup order. Later batches contain new
  // shared images only; file-identity deduplication belongs to graph discovery.
  // Startup uses the existing layout planner; late records have no TP offset.
  // Optional discovery metadata is deep-copied, including cyclic dependency
  // edges. Runtime graph discovery requires it for every resident image.
  long prepare(Transaction &, cpp::span<const ElfImage *const> images,
               cpp::span<const Discovery> discovery = {});
  // Retain the actual eager binding, not a later lookup result. Both ends must
  // belong to this candidate; only new requesters may be modified. Repeated
  // resolver preflight/write visits are allocation-free after the first edge.
  long record_binding(Transaction &, const Image *requester,
                      const Image *provider);
  long publish(Transaction &);
  bool can_publish(const Transaction &) const;
  const Snapshot *snapshot() const { return current; }
  // FIXME: Published descriptors and snapshots remain process-resident until
  // module reclamation supplies reader leases and coordinated finalization.
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
