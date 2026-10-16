//===-- Resident image queries ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_QUERIES_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_QUERIES_H

#include "hdr/types/dl_info.h"
#include "hdr/types/struct_dl_phdr_info.h"
#include "hdr/types/struct_link_map.h"
#include "hdr/types/struct_r_debug.h"
#include "loader/linux/runtime_scopes.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

class RuntimeQueries {
public:
  struct Record {
    const ImageRegistry::Image *image;
    link_map map;
    const Elf64_Phdr *headers;
  };
  struct Snapshot {
    const ImageRegistry::Snapshot *registry;
    cpp::span<Record> records;
    uint64_t adds = 0, subs = 0;
  };
  using PeekTLS = void *(*)(void *, const StartupTLSModule *);

private:
  SnapshotStorage<Snapshot> snapshots;
  Arena pending;
  SnapshotStorage<Snapshot>::Batch *batch = nullptr;
  Snapshot *candidate = nullptr;
  const Snapshot *current = nullptr;
  r_debug local_debug{};
  r_debug &debug;

public:
  explicit RuntimeQueries(Address page, r_debug *record = nullptr)
      : snapshots(page), pending(page), debug(record ? *record : local_debug) {}
  long prepare(const ImageRegistry::Snapshot &);
  void publish();
  long abort();
  const Snapshot *snapshot() const { return current; }
  using Lease = SnapshotStorage<Snapshot>::Lease;
  Lease acquire() { return Lease(snapshots); }
  long collect_snapshots() { return snapshots.collect(); }
  size_t active_readers() const { return snapshots.active_readers(); }
  bool readers_drained(const ImageRegistry::Image *image) const {
    return !snapshots.referenced(
        [&](const Snapshot &view) { return record(view, image) != nullptr; });
  }
  r_debug *debug_record() { return &debug; }
  // Called before main RELRO protection, never by a query or debugger callback.
  long bind_debug(const ElfImage &main, Address interpreter_bias);
  static long address(const Snapshot &, uintptr_t, Dl_info &);
  static const Record *record(const Snapshot &, const ImageRegistry::Image *);
  static long phdr_info(const Snapshot &, const Record &, void *, PeekTLS,
                        dl_phdr_info &);
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
