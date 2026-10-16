//===-- MMIX resident frame discovery ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_STARTUP_SECTIONS_H
#define LIBUNWIND_MMIX_STARTUP_SECTIONS_H

#include "StartupImages.h"
#include "CursorLifetime.h"

namespace libunwind {
template <typename A, typename S>
bool findMMIXStartupUnwindSections(A &addressSpace, uintptr_t pc, S &info) {
  MMIXUnwindImage view{};
  bool owned = false;
  if (!mmixFindCursorImage(pc, view, owned))
    return false;
  // Owned queries borrow their cursor's lease through later FDE/LSDA use.
  // Public raw cursors retain the conservative resident witness protocol.
  struct Query {
    uintptr_t token;
    ~Query() {
      if (token)
        __libunwind_mmix_release_startup_image(token);
    }
  } query{owned ? 0 : view.token};
  if (!view.token || !view.readable_ranges || !view.range_count ||
      view.readable_ranges % alignof(MMIXUnwindReadableRange) ||
      view.phdr_address % alignof(Elf64_Phdr) ||
      view.range_count > UINT16_MAX || view.phdr_count > UINT16_MAX ||
      !view.phdr_count || view.eh_frame_header_size < 12 ||
      view.eh_frame_header_size > UINT32_MAX)
    return false;
  const auto *ranges =
      reinterpret_cast<const MMIXUnwindReadableRange *>(view.readable_ranges);
  auto readable = [&](uintptr_t address, uintptr_t size) {
    for (uint64_t i = 0; i < view.range_count; ++i)
      if (mmixLinuxContains(ranges[i].address, ranges[i].size, address, size))
        return true;
    return false;
  };
  if (!readable(view.phdr_address, view.phdr_count * sizeof(Elf64_Phdr)) ||
      !readable(view.eh_frame_header, view.eh_frame_header_size))
    return false;
  const auto *phdr = reinterpret_cast<const Elf64_Phdr *>(view.phdr_address);
  bool containsPC = false;
  for (uint64_t i = 0; i < view.phdr_count; ++i) {
    const auto &p = phdr[i];
    if (p.p_type == PT_LOAD && (p.p_flags & PF_X) &&
        p.p_vaddr <= UINTPTR_MAX - view.load_bias &&
        mmixLinuxContains(view.load_bias + p.p_vaddr, p.p_filesz, pc, 1))
      containsPC = true;
  }
  if (!containsPC)
    return false;
  uintptr_t hdr = view.eh_frame_header;
  uint8_t ptr = addressSpace.get8(hdr + 1);
  uint8_t count = addressSpace.get8(hdr + 2);
  uint8_t table = addressSpace.get8(hdr + 3);
  if (addressSpace.get8(hdr) != 1 || (ptr != 0x1b && ptr != 0x1c) ||
      (count != 3 && count != 4) || (table != 0x3b && table != 0x3c))
    return false;
  uintptr_t prefix = 4 + (ptr == 0x1b ? 4 : 8) + (count == 3 ? 4 : 8);
  if (prefix > view.eh_frame_header_size)
    return false;
  typename EHHeaderParser<A>::EHHeaderInfo decoded;
  if (!EHHeaderParser<A>::decodeEHHdr(
          addressSpace, hdr, hdr + view.eh_frame_header_size, decoded) ||
      !decoded.fde_count ||
      decoded.fde_count >
          (view.eh_frame_header_size - prefix) / (table == 0x3b ? 8 : 16))
    return false;
  for (uint64_t i = 0; i < view.phdr_count; ++i) {
    const auto &p = phdr[i];
    if (p.p_type != PT_LOAD || !(p.p_flags & PF_R) ||
        p.p_vaddr > UINTPTR_MAX - view.load_bias)
      continue;
    uintptr_t start = view.load_bias + p.p_vaddr;
    if (!mmixLinuxContains(start, p.p_filesz, decoded.eh_frame_ptr, 4))
      continue;
    info.dso_base = view.load_bias;
    info.dwarf_section = decoded.eh_frame_ptr;
    info.dwarf_section_length = p.p_filesz - (decoded.eh_frame_ptr - start);
    info.dwarf_index_section = hdr;
    info.dwarf_index_section_length = view.eh_frame_header_size;
    return true;
  }
  return false;
}
} // namespace libunwind
#endif
