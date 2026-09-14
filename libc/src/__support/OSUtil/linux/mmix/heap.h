//===-- MMIX Linux allocator mappings ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_HEAP_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_HEAP_H

#include "src/__support/CPP/cstddef.h"
#include "src/__support/CPP/span.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// FIXME: Revisit this provisional Linux page contract when auxv/CRT is wired.
inline constexpr size_t HEAP_PAGE_SIZE = 8192;

// The caller owns the mapping, including the prefix reserved for its metadata.
// Copies are views, not independently releasable allocations. No implicit
// unmap.
struct HeapMapping {
  cpp::span<cpp::byte> storage;
  cpp::span<cpp::byte> heap;
};

ErrorOr<HeapMapping> map_heap_region(size_t size, size_t alignment,
                                     size_t metadata_size);

// Only unmap unpublished or empty regions, after removing any live references.
// Failure preserves the descriptor; success clears it. Neither function sets
// errno.
ErrorOr<int> unmap_heap_region(HeapMapping &mapping);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_HEAP_H
