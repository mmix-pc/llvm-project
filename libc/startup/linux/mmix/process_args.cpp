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
  if (seen != ALL || phdr % alignof(Elf64_Phdr) || phnum == 0 ||
      phnum > UINTPTR_MAX / sizeof(Elf64_Phdr) ||
      !user_range(phdr, phnum * sizeof(Elf64_Phdr)) || !user_range(random, 16))
    return false;
  const auto *headers = reinterpret_cast<const Elf64_Phdr *>(phdr);
  for (uintptr_t i = 0; i < phnum; ++i)
    if (headers[i].p_type == PT_TLS || headers[i].p_type == PT_INTERP ||
        headers[i].p_type == PT_DYNAMIC)
      return false;

  result.args = reinterpret_cast<Args *>(stack);
  result.env = reinterpret_cast<uintptr_t *>(env_address);
  result.aux = aux;
  result.random = reinterpret_cast<const unsigned char *>(random);
  result.page_size = page_size;
  return true;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
