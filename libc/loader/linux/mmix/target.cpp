//===-- MMIX Linux loader target admission --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

bool is_supported_target(const Elf64_Ehdr &header) {
  return header.e_machine == EM_MMIX &&
         header.e_ident[EI_CLASS] == ELFCLASS64 &&
         header.e_ident[EI_DATA] == ELFDATA2MSB &&
         (header.e_ident[EI_OSABI] == ELFOSABI_NONE ||
          header.e_ident[EI_OSABI] == ELFOSABI_LINUX) &&
         header.e_ident[EI_ABIVERSION] == 0 && header.e_flags == 0;
}

bool is_supported_page_size(Elf64_Xword page_size) { return page_size == 8192; }

bool is_user_range(Elf64_Addr address, Elf64_Xword size) {
  // Current Linux MMIX TASK_SIZE; keep platform limits out of the ELF parser.
  constexpr Elf64_Addr limit = Elf64_Addr(1) << 43;
  return address >= 8192 && address < limit && size <= limit - address;
}

bool is_aligned_entry(Elf64_Addr address) { return (address & 3) == 0; }

const char *interpreter_path() { return "/lib/ld-llvm-mmix.so.1"; }

bool is_relative_relocation(Elf64_Xword info) {
  // The upper half is the symbol index, which must be zero for RELATIVE.
  return info == 41; // R_MMIX_RELATIVE.
}

bool is_supported_dynamic_relocation(Elf64_Xword info) {
  if (is_relative_relocation(info))
    return true;
  // R_MMIX_64 and R_MMIX_GLOB_DAT require an ordinary dynamic symbol.
  Elf64_Word type = static_cast<Elf64_Word>(info);
  if (type == 44 || type == 46) // DTPMOD64/TPOFF64 may name the current module.
    return true;
  if (type == 45) // DTPOFF64 requires an actual TLS symbol.
    return (info >> 32) != 0;
  return (info >> 32) != 0 && (type == 5 || type == 42);
}

bool is_tls_relocation(Elf64_Xword info) {
  Elf64_Word type = static_cast<Elf64_Word>(info);
  return type >= 44 && type <= 46;
}

bool is_tls_module_relocation(Elf64_Xword info) {
  return static_cast<Elf64_Word>(info) == 44;
}

Elf64_Xword startup_tls_header_size() { return 16; }
Elf64_Xword startup_tls_min_alignment() { return 8; }

bool tls_relocation_value(Elf64_Xword info, Elf64_Xword module_id,
                          Elf64_Xword tp_offset, Elf64_Xword memory_size,
                          Elf64_Xword symbol_offset, Elf64_Xword addend,
                          Elf64_Xword &out) {
  if (!module_id || !is_tls_relocation(info))
    return false;
  if (is_tls_module_relocation(info)) {
    out = module_id;
    return !addend;
  }
  Elf64_Xword offset;
  if (addend >> 63) {
    Elf64_Xword magnitude = ~addend + 1;
    if (symbol_offset < magnitude)
      return false;
    offset = symbol_offset - magnitude;
  } else {
    if (addend > ~Elf64_Xword(0) - symbol_offset)
      return false;
    offset = symbol_offset + addend;
  }
  if (offset > memory_size)
    return false;
  if (static_cast<Elf64_Word>(info) == 46) {
    if (tp_offset > ~Elf64_Xword(0) - offset)
      return false;
    offset += tp_offset;
  }
  out = offset;
  return true;
}

bool runtime_relocation_value(Elf64_Xword info, Elf64_Addr base,
                              Elf64_Xword addend, Elf64_Addr &out) {
  if (!is_supported_dynamic_relocation(info) || is_tls_relocation(info))
    return false;
  if (addend >> 63) {
    Elf64_Xword magnitude = ~addend + 1;
    if (base < magnitude)
      return false;
    out = base - magnitude;
  } else if (__builtin_add_overflow(base, addend, &out)) {
    return false;
  }
  // Symbolic pointers may be null, notably unresolved weak references.
  // Otherwise both kinds must remain within the admitted user address range.
  return (!is_relative_relocation(info) && out == 0) || is_user_range(out, 1);
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
