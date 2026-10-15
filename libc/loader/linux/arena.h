//===-- Loader private bootstrap arena ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_ARENA_H
#define LLVM_LIBC_LOADER_LINUX_ARENA_H

#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Startup-only storage. Callers serialize access; concurrent loader use needs
// a bootstrap-safe lock before this arena can be shared between threads.
class Arena {
  struct Chunk {
    Chunk *next;
    size_t size;
    size_t used;
  };
  Chunk *chunks = nullptr;
  size_t page_size;

public:
  explicit Arena(size_t page_size) : page_size(page_size) {}
  Arena(const Arena &) = delete;
  Arena &operator=(const Arena &) = delete;
  // No individual free. Alignment must be a power of two <= page_size.
  long allocate(size_t size, size_t alignment, void *&out);
  // Transfer allocations without invalidating views. Both arenas must use the
  // same page size; the source is empty on success. No allocation or syscall.
  bool adopt(Arena &source);
  bool can_adopt(const Arena &source) const {
    return &source != this && page_size == source.page_size;
  }
  // Explicit release: report errors and retain failed mappings for retry.
  // All allocations are invalid after reset, even if some unmaps fail.
  long reset();
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_ARENA_H
