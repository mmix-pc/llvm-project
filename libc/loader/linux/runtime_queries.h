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
  };
  using PeekTLS = void *(*)(void *, const StartupTLSModule *);

private:
  Arena resident, pending;
  Snapshot *candidate = nullptr;
  const Snapshot *current = nullptr;
  r_debug local_debug{};
  r_debug &debug;

public:
  explicit RuntimeQueries(Address page, r_debug *record = nullptr)
      : resident(page), pending(page), debug(record ? *record : local_debug) {}
  long prepare(const ImageRegistry::Snapshot &);
  void publish();
  long abort();
  const Snapshot *snapshot() const { return current; }
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
