//===-- MMIX Linux allocator mappings
//--------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "heap.h"
#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/sys_mman_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mmap.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/munmap.h"
#include "src/__support/block.h"
#include "src/__support/freestore.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static_assert(HEAP_PAGE_SIZE % BlockRef::MIN_ALIGN == 0);

ErrorOr<HeapMapping> map_heap_region(size_t size, size_t alignment,
                                     size_t metadata_size) {
  if (!alignment || (alignment & (alignment - 1)))
    return Error(EINVAL);

  constexpr size_t LIMIT = cpp::numeric_limits<ptrdiff_t>::max();
  if (size > LIMIT || alignment > LIMIT ||
      metadata_size > LIMIT - (BlockRef::MIN_ALIGN - 1))
    return Error(ENOMEM);
  size_t prefix =
      (metadata_size + BlockRef::MIN_ALIGN - 1) & ~(BlockRef::MIN_ALIGN - 1);
  alignment = cpp::max(alignment, BlockRef::MIN_ALIGN);
  size_t inner = BlockRef::min_size_for_allocation(
      alignment, cpp::max(size, FreeStore::MIN_INNER_SIZE));
  // Include the initial/sentinel headers and worst-case block alignment loss.
  constexpr size_t OVERHEAD = 2 * (BlockRef::HEADER_SIZE + BlockRef::MIN_ALIGN);
  if (!inner || inner > LIMIT - OVERHEAD || prefix > LIMIT - OVERHEAD - inner)
    return Error(ENOMEM);
  size_t bytes = prefix + inner + OVERHEAD;
  if (bytes > LIMIT - (HEAP_PAGE_SIZE - 1))
    return Error(ENOMEM);
  bytes = (bytes + HEAP_PAGE_SIZE - 1) & ~(HEAP_PAGE_SIZE - 1);

  auto result = linux_syscalls::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!result)
    return Error(result.error());
  uintptr_t address = reinterpret_cast<uintptr_t>(result.value());
  // Validate the usable span before publishing it to FreeListHeap's lazy init.
  if (!address || address % HEAP_PAGE_SIZE || address > LIMIT - bytes) {
    auto released = linux_syscalls::munmap(result.value(), bytes);
    return Error(released ? ENOMEM : released.error());
  }
  auto *base = static_cast<cpp::byte *>(result.value());
  return HeapMapping{{base, bytes}, {base + prefix, bytes - prefix}};
}

ErrorOr<int> unmap_heap_region(HeapMapping &mapping) {
  if (mapping.storage.empty())
    return 0;
  auto result =
      linux_syscalls::munmap(mapping.storage.data(), mapping.storage.size());
  if (result)
    mapping = {};
  return result;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
