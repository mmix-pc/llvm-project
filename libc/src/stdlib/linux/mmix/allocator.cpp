//===-- MMIX Linux region allocator ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "allocator.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/mmix/heap.h"
#include "src/__support/freelist_heap.h"
#include "src/__support/threads/raw_mutex.h"

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

// One private, nonallocating lock protects both the list and each region's heap.
// Allocation from signal handlers is unsupported. FIXME: Coordinate this lock
// with fork when fork from a multithreaded process is admitted.
LIBC_CONSTINIT RawMutex regions_mutex;
Region *regions = nullptr;

Region **find_region(void *ptr) {
  uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
  for (Region **link = &regions; *link; link = &(*link)->next) {
    uintptr_t begin = reinterpret_cast<uintptr_t>((*link)->mapping.heap.data());
    if (address >= begin && address - begin < (*link)->mapping.heap.size())
      return link;
  }
  return nullptr;
}

void release_empty(Region **link) {
  Region *region = *link;
  Region *next = region->next;
  HeapMapping mapping = region->mapping;
  *link = next;
  // The mapping owns Region; do not read it after a successful unmap.
  if (!unmap_heap_region(mapping))
    *link = region;
}

void *allocate_unlocked(size_t size, size_t alignment = 1) {
  constexpr size_t LIMIT = cpp::numeric_limits<ptrdiff_t>::max();
  if (!size || !alignment || (alignment & (alignment - 1)) ||
      alignment > LIMIT || size > LIMIT - (alignment - 1))
    return nullptr;
  // FreeListHeap requires a size multiple; POSIX callers need not supply one.
  size = (size + alignment - 1) & ~(alignment - 1);
  for (Region *region = regions; region; region = region->next) {
    if (void *ptr = region->heap.aligned_allocate(alignment, size)) {
      ++region->live;
      return ptr;
    }
  }

  auto mapping = map_heap_region(size, alignment, sizeof(Region));
  if (!mapping)
    return nullptr;
  auto *region = new (mapping->storage.data()) Region(*mapping, regions);
  regions = region;
  if (void *ptr = region->heap.aligned_allocate(alignment, size)) {
    region->live = 1;
    return ptr;
  }
  release_empty(&regions);
  return nullptr;
}

void deallocate_unlocked(void *ptr) {
  if (!ptr)
    return;
  if (Region **link = find_region(ptr)) {
    Region *region = *link;
    region->heap.free(ptr);
    --region->live;
    if (!region->live)
      release_empty(link);
    return;
  }
  LIBC_ASSERT(false && "allocation does not belong to this heap");
}

void *resize_unlocked(void *ptr, size_t size) {
  if (!size) {
    deallocate_unlocked(ptr);
    return nullptr;
  }
  if (!ptr)
    return allocate_unlocked(size);
  if (size > size_t(cpp::numeric_limits<ptrdiff_t>::max()))
    return nullptr;
  Region **link = find_region(ptr);
  if (!link) {
    LIBC_ASSERT(false && "allocation does not belong to this heap");
    return nullptr;
  }
  Region *region = *link;
  size_t old_size = region->heap.allocation_size(ptr);
  if (void *resized = region->heap.realloc(ptr, size))
    return resized;

  // FreeListHeap resizes within one region. Keep the original allocation live
  // until another region supplies storage and its contents have been copied.
  void *resized = allocate_unlocked(size);
  if (!resized)
    return nullptr;
  inline_memcpy(resized, ptr, cpp::min(old_size, size));
  deallocate_unlocked(ptr);
  return resized;
}

} // namespace

void *allocate(size_t size, size_t alignment) {
  cpp::lock_guard lock(regions_mutex);
  return allocate_unlocked(size, alignment);
}

void deallocate(void *ptr) {
  cpp::lock_guard lock(regions_mutex);
  deallocate_unlocked(ptr);
}

void *resize(void *ptr, size_t size) {
  cpp::lock_guard lock(regions_mutex);
  return resize_unlocked(ptr, size);
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
