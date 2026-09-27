//===-- MMIX Linux argument parsing ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "process_args.h"
#include "hdr/elf_proxy.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static_assert(sizeof(uintptr_t) == 8 && sizeof(auxv::Entry) == 16);
static_assert(sizeof(Elf64_Phdr) == 56);

static bool user_range(uintptr_t address, uintptr_t size) {
  constexpr uintptr_t MAX_USER = UINTPTR_MAX >> 1;
  return address != 0 && address <= MAX_USER && size != 0 &&
         size - 1 <= MAX_USER - address;
}

static bool segment_range(const Elf64_Phdr &p) {
  constexpr uintptr_t MAX_USER = UINTPTR_MAX >> 1;
  uintptr_t align = p.p_align ? p.p_align : 1;
  return p.p_filesz <= p.p_memsz && p.p_filesz <= UINTPTR_MAX - p.p_offset &&
         p.p_vaddr <= MAX_USER && p.p_memsz <= MAX_USER - p.p_vaddr &&
         (!p.p_memsz || p.p_vaddr != 0) && align <= MAX_USER &&
         !(align & (align - 1)) &&
         p.p_offset % align == p.p_vaddr % align;
}

// Validate both coverage and correspondence, including overlapping loads.
// The kernel still owns actual mapping accessibility and permissions.
static bool file_coverage(const Elf64_Phdr *headers, uintptr_t count,
                          uintptr_t address, uintptr_t size, uintptr_t offset) {
  if (size > UINTPTR_MAX - offset || size > (UINTPTR_MAX >> 1) - address)
    return false;
  uintptr_t end = address + size;
  for (uintptr_t i = 0; i < count; ++i) {
    const auto &p = headers[i];
    if (p.p_type != PT_LOAD)
      continue;
    uintptr_t begin = address > p.p_vaddr ? address : p.p_vaddr;
    uintptr_t limit = end < p.p_vaddr + p.p_memsz ? end : p.p_vaddr + p.p_memsz;
    if (begin >= limit)
      continue;
    if (!(p.p_flags & PF_R) || limit > p.p_vaddr + p.p_filesz ||
        offset + (begin - address) != p.p_offset + (begin - p.p_vaddr))
      return false;
  }
  for (uintptr_t cursor = address; cursor < end;) {
    uintptr_t next = cursor;
    for (uintptr_t i = 0; i < count; ++i) {
      const auto &p = headers[i];
      if (p.p_type == PT_LOAD && (p.p_flags & PF_R) && p.p_vaddr <= cursor &&
          p.p_vaddr + p.p_filesz > next)
        next = p.p_vaddr + p.p_filesz;
    }
    if (next == cursor)
      return false;
    cursor = next;
  }
  return true;
}

static bool tls_extent(const TLSImage &tls) {
  // Bound the TCB, image, over-page alignment slack and final page rounding.
  constexpr uintptr_t MAX_EXTENT = UINTPTR_MAX >> 1;
  uintptr_t residue = tls.address % tls.align;
  uintptr_t header_residue = 16 % tls.align;
  uintptr_t padding = residue >= header_residue
                          ? residue - header_residue
                          : tls.align - (header_residue - residue);
  if (padding > MAX_EXTENT - 16 || tls.size > MAX_EXTENT - 16 - padding)
    return false;
  uintptr_t extent = 16 + padding + tls.size;
  uintptr_t slack = tls.align > 8192 ? tls.align - 1 : 0;
  if (slack > MAX_EXTENT - extent)
    return false;
  extent += slack;
  uintptr_t rounding = (8192 - extent % 8192) % 8192;
  return rounding <= MAX_EXTENT - extent;
}

bool parse_process_args(uintptr_t *stack, ProcessArgs &result) {
  uintptr_t address = reinterpret_cast<uintptr_t>(stack);
  if (address % 8 || !user_range(address, 8))
    return false;
  uintptr_t argc = *stack;
  if (argc > __INT_MAX__ || !user_range(address, (argc + 2) * 8))
    return false;
  for (uintptr_t i = 0; i < argc; ++i)
    if (!user_range(stack[i + 1], 1))
      return false;
  if (stack[argc + 1] != 0)
    return false;

  uintptr_t env_address = address + (argc + 2) * 8;
  uintptr_t cursor = env_address;
  for (;;) {
    if (!user_range(cursor, 8))
      return false;
    uintptr_t value = *reinterpret_cast<const uintptr_t *>(cursor);
    cursor += 8;
    if (value == 0)
      break;
    if (!user_range(value, 1))
      return false;
  }
  const auto *aux = reinterpret_cast<const auxv::Entry *>(cursor);
  uintptr_t phdr = 0, phnum = 0, random = 0, page_size = 0;
  enum RequiredField : unsigned {
    PHDR = 1 << 0,
    PHNUM = 1 << 1,
    PHENT = 1 << 2,
    PAGESZ = 1 << 3,
    RANDOM = 1 << 4,
    BASE = 1 << 5,
    ALL = PHDR | PHNUM | PHENT | PAGESZ | RANDOM,
  };
  unsigned seen = 0;
  for (;;) {
    if (!user_range(cursor, sizeof(auxv::Entry)))
      return false;
    const auto &entry = *reinterpret_cast<const auxv::Entry *>(cursor);
    cursor += sizeof(auxv::Entry);
    if (entry.type == AT_NULL) {
      if (entry.val != 0)
        return false;
      break;
    }
    unsigned bit = 0;
    switch (entry.type) {
    case AT_PHDR:
      bit = PHDR;
      phdr = entry.val;
      break;
    case AT_PHNUM:
      bit = PHNUM;
      phnum = entry.val;
      break;
    case AT_PHENT:
      bit = PHENT;
      if (entry.val != sizeof(Elf64_Phdr))
        return false;
      break;
    case AT_PAGESZ:
      bit = PAGESZ;
      page_size = entry.val;
      if (entry.val != 8192)
        return false;
      break;
    case AT_RANDOM:
      bit = RANDOM;
      random = entry.val;
      break;
    case AT_BASE:
      bit = BASE;
      if (entry.val != 0)
        return false;
      break;
    default:
      break;
    }
    if (seen & bit)
      return false;
    seen |= bit;
  }
  if ((seen & ALL) != ALL || phdr % alignof(Elf64_Phdr) || phnum == 0 ||
      phnum > UINTPTR_MAX / sizeof(Elf64_Phdr) ||
      !user_range(phdr, phnum * sizeof(Elf64_Phdr)) || !user_range(random, 16))
    return false;
  const auto *headers = reinterpret_cast<const Elf64_Phdr *>(phdr);
  const Elf64_Phdr *tls = nullptr, *table = nullptr;
  for (uintptr_t i = 0; i < phnum; ++i) {
    const auto &p = headers[i];
    if (p.p_type == PT_INTERP || p.p_type == PT_DYNAMIC)
      return false;
    if (p.p_type == PT_LOAD || p.p_type == PT_TLS || p.p_type == PT_PHDR) {
      if (!segment_range(p))
        return false;
    }
    if (p.p_type == PT_LOAD && p.p_offset % page_size != p.p_vaddr % page_size)
      return false;
    if (p.p_type == PT_TLS) {
      if (tls)
        return false;
      tls = &p;
    }
    if (p.p_type == PT_PHDR) {
      if (table)
        return false;
      table = &p;
    }
  }
  uintptr_t table_size = phnum * sizeof(Elf64_Phdr);
  if (table) {
    if (table->p_vaddr != phdr || table->p_filesz < table_size ||
        !file_coverage(headers, phnum, phdr, table->p_filesz, table->p_offset))
      return false;
  } else {
    // Without PT_PHDR, derive correspondence from a load, not an ELF header.
    bool covered = false;
    for (uintptr_t i = 0; i < phnum; ++i) {
      const auto &p = headers[i];
      if (p.p_type == PT_LOAD && (p.p_flags & PF_R) && p.p_vaddr <= phdr &&
          phdr - p.p_vaddr < p.p_filesz) {
        covered = file_coverage(headers, phnum, phdr, table_size,
                                p.p_offset + (phdr - p.p_vaddr));
        break;
      }
    }
    if (!covered)
      return false;
  }
  TLSImage image{0, 0, 0, 1};
  if (tls) {
    image = {tls->p_vaddr, tls->p_memsz, tls->p_filesz,
             tls->p_align ? tls->p_align : 1};
    if (!tls_extent(image) ||
        (image.init_size && !file_coverage(headers, phnum, image.address,
                                          image.init_size, tls->p_offset)))
      return false;
  }

  result.args = reinterpret_cast<Args *>(stack);
  result.env = reinterpret_cast<uintptr_t *>(env_address);
  result.aux = aux;
  result.random = reinterpret_cast<const unsigned char *>(random);
  result.page_size = page_size;
  result.phdrs = headers;
  result.phnum = phnum;
  result.tls = image;
  result.has_tls = tls != nullptr;
  return true;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
