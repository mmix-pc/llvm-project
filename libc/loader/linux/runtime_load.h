//===-- Late image transactions ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_LOAD_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_LOAD_H

#include "loader/linux/runtime_graph.h"
#include "loader/linux/runtime_scopes.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// One explicit transaction, held under mutation serialization and cancellation
// suppression through prepare/commit/abort. No constructors or application code
// run here. Readers acquire a RuntimeScopes::Lease, which includes the
// corresponding image/TLS registry and unwind set; ImageRegistry access remains
// writer-only.
class RuntimeLoad {
  Address page_size;
  RuntimeGraph graph;
  Arena scratch;
  ImageRegistry::Transaction images;
  RuntimeScopes::Transaction scopes;
  ImageRegistry *registry = nullptr;
  RuntimeScopes *namespace_owner = nullptr;
  const ImageRegistry::Snapshot *base = nullptr;
  bool active = false, ready = false, committed = false, added = false;
  uint64_t handle = 0;

  long prepare_records(const ImageRegistry::Image &caller, bool global);

public:
  explicit RuntimeLoad(Address page)
      : page_size(page), graph(page), scratch(page), images(page),
        scopes(page) {}
  RuntimeLoad(const RuntimeLoad &) = delete;
  RuntimeLoad &operator=(const RuntimeLoad &) = delete;
  MapResult prepare(ImageRegistry &, RuntimeScopes &,
                    const ImageRegistry::Image &caller, Bytes name,
                    const SearchEnvironment &, bool global,
                    bool no_load = false);
  // No allocation, syscall or user callback after the commit precheck. The
  // scope release-store is the sole reader-visible publication point.
  long commit(uint64_t &token);
  long abort();
  long release_mapping(Address bias) {
    return committed ? graph.release_mapping(bias) : -EINVAL;
  }
  long collect_storage();
  bool owns_mapping(Address bias) const {
    return committed && graph.owns_mapping(bias);
  }
  const RuntimeScopes::Snapshot *view() const {
    return ready ? scopes.view() : nullptr;
  }
  // Keep this control alive through coordinated release, including failed
  // unmap retries; collect_storage releases payloads, not this object.
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
