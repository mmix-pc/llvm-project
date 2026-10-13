//===-- Transactional loader DSO mappings ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_MAPPING_H
#define LLVM_LIBC_LOADER_LINUX_MAPPING_H

#include "loader/linux/arena.h"
#include "loader/linux/dynamic.h"
#include "loader/linux/io.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

struct MapResult {
  long error;
  long cleanup_error;
};

// Owns only mappings acquired here, never kernel-owned main/interpreter images.
// Startup callers serialize access and keep the input file stable while
// loading. Like Arena, requires explicit reset before destruction; failed
// unmaps retain ownership for retry. No view survives reset, including a failed
// reset.
class DsoMapping {
  struct Range {
    Address address = 0, size = 0;
    long release();
  };
  Address page_size;
  OwnedFd fd;
  Arena arena;
  Range staging, reservation;
  bool arena_owned = false, ready = false;
  ElfImage mapped;
  DynamicInfo dynamic;

  long populate();
  MapResult finish_open();
  bool busy() const;
  long map_segment(const Elf64_Phdr &p, Address bias);
  long replace(Address address, Address size, int prot, bool anonymous,
               Address offset);
  long release_image();

public:
  explicit DsoMapping(Address page_size)
      : page_size(page_size), arena(page_size) {}
  DsoMapping(const DsoMapping &) = delete;
  DsoMapping &operator=(const DsoMapping &) = delete;
  // Exact path only. A successful result means mapped, not relocated/published.
  // Errors use negative Linux errno; malformed/unsupported ELF uses ENOEXEC.
  MapResult open(const char *path);
  // Transfer an already identified file without reopening its pathname.
  MapResult open(OwnedFd &source);
  long reset();
  const ElfImage *image() const { return ready ? &mapped : nullptr; }
  const DynamicInfo *metadata() const { return ready ? &dynamic : nullptr; }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_MAPPING_H
