//===-- MMIX Linux frame provider interface -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_LINUX_H
#define LIBUNWIND_MMIX_LINUX_H

#include <linux/elf.h>

#include "../EHHeaderParser.hpp"

extern "C" const Elf64_Ehdr __ehdr_start;

namespace libunwind {

inline bool mmixLinuxContains(uintptr_t start, uintptr_t size, uintptr_t addr,
                             uintptr_t length) {
  return size <= UINTPTR_MAX - start && addr >= start && length <= size &&
         addr - start <= size - length;
}

// The main static ET_EXEC image is mapped by the kernel. Section headers need
// not be mapped (or even retained), so use only its program headers.
// FIXME: Add loader-owned image enumeration when shared objects are supported.
template <typename A, typename S>
bool findMMIXLinuxUnwindSections(A &addressSpace, const Elf64_Ehdr &elf,
                                uintptr_t targetAddr, S &info) {
  if (elf.e_ident[EI_MAG0] != ELFMAG0 || elf.e_ident[EI_MAG1] != ELFMAG1 ||
      elf.e_ident[EI_MAG2] != ELFMAG2 || elf.e_ident[EI_MAG3] != ELFMAG3 ||
      elf.e_ident[EI_CLASS] != ELFCLASS64 ||
      elf.e_ident[EI_DATA] != ELFDATA2MSB ||
      elf.e_ident[EI_VERSION] != EV_CURRENT || elf.e_version != EV_CURRENT ||
      elf.e_machine != EM_MMIX || elf.e_type != ET_EXEC ||
      elf.e_ehsize != sizeof(Elf64_Ehdr) ||
      elf.e_phentsize != sizeof(Elf64_Phdr) || !elf.e_phnum ||
      elf.e_phnum == PN_XNUM)
    return false;

  uintptr_t base = reinterpret_cast<uintptr_t>(&elf);
  uintptr_t phSize = elf.e_phnum * sizeof(Elf64_Phdr);
  if (elf.e_phoff < sizeof(Elf64_Ehdr) ||
      elf.e_phoff % alignof(Elf64_Phdr) ||
      elf.e_phoff > UINTPTR_MAX - base ||
      phSize > UINTPTR_MAX - (base + elf.e_phoff))
    return false;
  const auto *phdr =
      reinterpret_cast<const Elf64_Phdr *>(base + elf.e_phoff);
  const Elf64_Phdr *eh = nullptr;
  bool foundPC = false, foundHeaders = false;
  for (unsigned i = 0; i < elf.e_phnum; ++i) {
    const auto &p = phdr[i];
    if (p.p_type == PT_INTERP || p.p_type == PT_DYNAMIC || p.p_type == PT_TLS)
      return false;
    if (p.p_type == PT_GNU_EH_FRAME) {
      if (eh)
        return false;
      eh = &p;
    }
    if (p.p_type != PT_LOAD)
      continue;
    if (p.p_filesz > p.p_memsz)
      return false;
    if ((p.p_flags & PF_X) &&
        mmixLinuxContains(p.p_vaddr, p.p_filesz, targetAddr, 1))
      foundPC = true;
    if ((p.p_flags & PF_R) && p.p_offset == 0 && p.p_vaddr == base &&
        mmixLinuxContains(base, p.p_filesz, base + elf.e_phoff, phSize))
      foundHeaders = true;
  }
  if (!foundPC || !foundHeaders || !eh || eh->p_filesz < 12 ||
      eh->p_filesz > UINT32_MAX || eh->p_filesz > eh->p_memsz)
    return false;

  bool foundIndex = false;
  for (unsigned i = 0; i < elf.e_phnum; ++i) {
    const auto &p = phdr[i];
    if (p.p_type == PT_LOAD && (p.p_flags & PF_R) &&
        mmixLinuxContains(p.p_vaddr, p.p_filesz, eh->p_vaddr, eh->p_filesz))
      foundIndex = true;
  }
  if (!foundIndex)
    return false;

  // lld's static index uses bounded fixed-width encodings. Reject other
  // layouts before the generic parser can abort or follow an indirect pointer.
  uintptr_t hdr = eh->p_vaddr;
  if (addressSpace.get8(hdr) != 1 ||
      addressSpace.get8(hdr + 1) != (DW_EH_PE_pcrel | DW_EH_PE_sdata4) ||
      addressSpace.get8(hdr + 2) != DW_EH_PE_udata4 ||
      addressSpace.get8(hdr + 3) != (DW_EH_PE_datarel | DW_EH_PE_sdata4))
    return false;
  typename EHHeaderParser<A>::EHHeaderInfo decoded;
  if (!EHHeaderParser<A>::decodeEHHdr(addressSpace, hdr, hdr + eh->p_filesz,
                                     decoded) ||
      !decoded.fde_count || decoded.fde_count > (eh->p_filesz - 12) / 8)
    return false;

  for (unsigned i = 0; i < elf.e_phnum; ++i) {
    const auto &p = phdr[i];
    if (p.p_type != PT_LOAD || !(p.p_flags & PF_R) ||
        !mmixLinuxContains(p.p_vaddr, p.p_filesz, decoded.eh_frame_ptr, 4))
      continue;
    info.dso_base = base;
    info.dwarf_section = decoded.eh_frame_ptr;
    // Bound the zero-terminated frame sequence by its file-backed mapping.
    info.dwarf_section_length = p.p_filesz - (decoded.eh_frame_ptr - p.p_vaddr);
    info.dwarf_index_section = hdr;
    info.dwarf_index_section_length = eh->p_filesz;
    return true;
  }
  return false;
}
} // namespace libunwind

#endif
