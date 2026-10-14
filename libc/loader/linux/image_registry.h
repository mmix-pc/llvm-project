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
  struct Image {
    ElfImage elf;
    StartupTLSModule tls;
    bool startup;
  };
  struct Snapshot {
    uint64_t generation;
    cpp::span<const Image *const> images;
    const Image *find(uint64_t id, uint64_t module_generation) const;
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
  long prepare(Transaction &, cpp::span<const ElfImage *const> images);
  long publish(Transaction &);
  const Snapshot *snapshot() const { return current; }
  // FIXME: Published descriptors and snapshots remain process-resident until
  // module reclamation supplies reader leases and coordinated finalization.
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
