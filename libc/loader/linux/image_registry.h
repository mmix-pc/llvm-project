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
#include "loader/linux/dynamic.h"
#include "loader/linux/io.h"
#include "loader/linux/module_ownership.h"
#include "loader/linux/search.h"
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
  struct Storage;
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
    // Deep-copied search metadata, not a code-lifetime edge to the requester.
    cpp::span<const SearchObject> ancestry{};
    cpp::span<const Image *const> dependencies{};
    // Written only in the private transaction, before relocation completes.
    mutable const Binding *bindings = nullptr;
    // Validated before publication; borrowed bytes share this image's lifetime.
    // Scope snapshots must not repeat whole-image relocation validation.
    DynamicInfo dynamic{};
    const DynamicInfo *metadata() const {
      return dynamic.is_valid() ? &dynamic : nullptr;
    }
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
    bool same_images(const Snapshot &other) const {
      if (generation != other.generation || images.size() != other.images.size())
        return false;
      for (size_t i = 0; i < images.size(); ++i)
        if (images[i] != other.images[i])
          return false;
      return true;
    }
    const Image *find(uint64_t id, uint64_t module_generation) const;
    // Caller holds mutation serialization. Explicit roots supplement permanent
    // roots and external ownership tickets. Edges are not reference-counted:
    // unreachable cycles belong to the returned set together. Finalization
    // may ignore owner-local process callbacks, which it must drain itself;
    // physical retirement always includes every ticket. Does not retire
    // or unmap; storage belongs to scratch and must not escape its lifetime.
    long reclaimable(Arena &scratch, cpp::span<const Image *const> roots,
                     cpp::span<const Image *const> &out,
                     bool for_finalization = false) const;
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
    Arena descriptors;
    Storage *storage = nullptr;
    State state = State::Empty;
    const ImageRegistry *owner = nullptr;
    const Snapshot *base = nullptr;
    Snapshot *candidate = nullptr;

  public:
    explicit Transaction(size_t page_size)
        : arena(page_size), descriptors(page_size) {}
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
  struct Storage {
    Arena arena;
    cpp::span<Image> records;
    Storage *next = nullptr;
    bool detached = false;
    explicit Storage(size_t page) : arena(page) {}
  };
  size_t page;
  Arena resident;
  Storage *batches = nullptr;
  RegistrySequence ids, generations;
  const Snapshot *current = nullptr;
  RetirementEvents *events;

public:
  explicit ImageRegistry(size_t page_size, RetirementEvents *events = nullptr)
      : page(page_size), resident(page_size), events(events) {}
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
  // Writer-only synchronization after scope removal; copy before freeing a
  // publication batch. Requires a subset of the current image set.
  long adopt_removal(const Snapshot &);
  // Only called after reader drain and mark_reclaimed for affected images.
  // Stable image/identity controls remain, but paths, edges and ELF metadata
  // storage are released when the last image in their batch is reclaimed.
  long collect_reclaimed();
  // Stable identity controls remain process-owned; payload collection requires
  // coordinated finalization and drained reader domains.
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
