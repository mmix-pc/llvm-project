//===-- MMIX Linux region allocator
//----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "allocator.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/mmix/heap.h"
#include "src/__support/freelist_heap.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {

struct Region {
  HeapMapping mapping;
  Region *next;
  size_t live = 0;
  FreeListHeap heap;

  Region(HeapMapping mapping, Region *next)
      : mapping(mapping), next(next), heap(mapping.heap) {}
};

static_assert(alignof(Region) <= BlockRef::MIN_ALIGN);

// FIXME: This process-global list requires single-threaded, non-reentrant use.
// Revisit synchronization and fork handling with the pthread runtime.
Region *regions = nullptr;

void release_empty(Region **link) {
  Region *region = *link;
  Region *next = region->next;
  HeapMapping mapping = region->mapping;
  *link = next;
  // The mapping owns Region; do not read it after a successful unmap.
  if (!unmap_heap_region(mapping))
    *link = region;
}

} // namespace

void *allocate(size_t size) {
  if (!size || size > size_t(cpp::numeric_limits<ptrdiff_t>::max()))
    return nullptr;
  for (Region *region = regions; region; region = region->next) {
    if (void *ptr = region->heap.allocate(size)) {
      ++region->live;
      return ptr;
    }
  }

  auto mapping = map_heap_region(size, BlockRef::MIN_ALIGN, sizeof(Region));
  if (!mapping)
    return nullptr;
  auto *region = new (mapping->storage.data()) Region(*mapping, regions);
  regions = region;
  if (void *ptr = region->heap.allocate(size)) {
    region->live = 1;
    return ptr;
  }
  release_empty(&regions);
  return nullptr;
}

void deallocate(void *ptr) {
  if (!ptr)
    return;
  uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
  for (Region **link = &regions; *link; link = &(*link)->next) {
    Region *region = *link;
    uintptr_t begin = reinterpret_cast<uintptr_t>(region->mapping.heap.data());
    if (address < begin || address - begin >= region->mapping.heap.size())
      continue;
    region->heap.free(ptr);
    --region->live;
    if (!region->live)
      release_empty(link);
    return;
  }
  LIBC_ASSERT(false && "allocation does not belong to this heap");
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
