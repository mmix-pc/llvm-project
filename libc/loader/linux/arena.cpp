//===-- Loader private bootstrap arena ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/arena.h"
#include "hdr/errno_macros.h"
#include "hdr/sys_mman_macros.h"
#include "loader/linux/syscall.h"
#include "src/__support/CPP/new.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool power_of_two(size_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

long Arena::allocate(size_t size, size_t alignment, void *&out) {
  out = nullptr;
  if (!power_of_two(page_size) || page_size > __LONG_MAX__ ||
      !power_of_two(alignment) || alignment > page_size || size == 0)
    return -EINVAL;
  constexpr size_t max = __LONG_MAX__;
  if (size > max - sizeof(Chunk) ||
      size + sizeof(Chunk) > max - (alignment - 1))
    return -EOVERFLOW;
  for (Chunk *chunk = chunks; chunk; chunk = chunk->next) {
    size_t offset = (chunk->used + alignment - 1) & ~(alignment - 1);
    if (offset <= chunk->size && size <= chunk->size - offset) {
      chunk->used = offset + size;
      out = reinterpret_cast<unsigned char *>(chunk) + offset;
      return 0;
    }
  }
  size_t needed = sizeof(Chunk) + alignment - 1 + size;
  if (needed > max - (page_size - 1))
    return -EOVERFLOW;
  size_t bytes = (needed + page_size - 1) & ~(page_size - 1);
  long result =
      raw_syscall(SYS_mmap, 0, static_cast<long>(bytes), PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (result < 0)
    return result;
  size_t offset = (sizeof(Chunk) + alignment - 1) & ~(alignment - 1);
  auto *chunk = new (reinterpret_cast<void *>(result))
      Chunk{chunks, bytes, offset + size};
  chunks = chunk;
  out = reinterpret_cast<unsigned char *>(chunk) + offset;
  return 0;
}

bool Arena::adopt(Arena &source) {
  if (&source == this || source.page_size != page_size)
    return false;
  if (source.chunks) {
    Chunk *tail = source.chunks;
    while (tail->next)
      tail = tail->next;
    tail->next = chunks;
    chunks = source.chunks;
    source.chunks = nullptr;
  }
  return true;
}

long Arena::reset() {
  long error = 0;
  Chunk *chunk = chunks;
  chunks = nullptr;
  while (chunk) {
    Chunk *next = chunk->next;
    long result = raw_syscall(SYS_munmap, reinterpret_cast<long>(chunk),
                              static_cast<long>(chunk->size));
    if (result < 0) {
      if (error == 0)
        error = result;
      chunk->used = sizeof(Chunk);
      chunk->next = chunks;
      chunks = chunk;
    }
    chunk = next;
  }
  return error;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
