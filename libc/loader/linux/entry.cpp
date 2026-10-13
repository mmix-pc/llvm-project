//===-- Loader initial stack and image validation -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/entry.h"
#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool word(Bytes bytes, Address &cursor, bool big, Address &out) {
  if (bytes.size() < 8 || cursor > bytes.size() - 8)
    return false;
  out = 0;
  for (unsigned i = 0; i < 8; ++i)
    out = (out << 8) | bytes[cursor + (big ? i : 7 - i)];
  cursor += 8;
  return true;
}

static AuxField field(Address tag) {
  switch (tag) {
  case AT_PHDR:
    return AuxField::Phdr;
  case AT_PHENT:
    return AuxField::Phent;
  case AT_PHNUM:
    return AuxField::Phnum;
  case AT_PAGESZ:
    return AuxField::Pagesz;
  case AT_BASE:
    return AuxField::Base;
  case AT_ENTRY:
    return AuxField::Entry;
  case AT_RANDOM:
    return AuxField::Random;
  case AT_SECURE:
    return AuxField::Secure;
  case AT_UID:
    return AuxField::Uid;
  case AT_EUID:
    return AuxField::Euid;
  case AT_GID:
    return AuxField::Gid;
  case AT_EGID:
    return AuxField::Egid;
  default:
    return AuxField::Count;
  }
}

EntryError decode_initial_stack(Bytes bytes, Address sp, bool big,
                                InitialStack &out) {
  out.valid = false;
  if (!bytes.data() || bytes.size() < 8 || (sp & 7) ||
      !is_user_range(sp, bytes.size()))
    return EntryError::Stack;
  InitialStack state;
  state.original_sp = sp;
  state.big_endian = big;
  Address cursor = 0, value;
  if (!word(bytes, cursor, big, state.argc) || state.argc > __INT_MAX__ ||
      state.argc > (bytes.size() - cursor) / 8)
    return EntryError::Arguments;
  state.argv = sp + cursor;
  for (Address i = 0; i < state.argc; ++i)
    if (!word(bytes, cursor, big, value) || !is_user_range(value, 1))
      return EntryError::Arguments;
  if (!word(bytes, cursor, big, value) || value != 0)
    return EntryError::Arguments;
  state.envp = sp + cursor;
  do {
    if (!word(bytes, cursor, big, value))
      return EntryError::Environment;
    if (value && !is_user_range(value, 1))
      return EntryError::Environment;
  } while (value);
  state.auxv = sp + cursor;
  unsigned seen = 0;
  for (;;) {
    Address tag;
    if (!word(bytes, cursor, big, tag) || !word(bytes, cursor, big, value))
      return EntryError::Auxv;
    if (tag == AT_NULL) {
      if (value != 0)
        return EntryError::Auxv;
      break;
    }
    AuxField f = field(tag);
    if (f == AuxField::Count)
      continue;
    unsigned index = static_cast<unsigned>(f);
    unsigned bit = 1U << index;
    if (seen & bit)
      return EntryError::Duplicate;
    if (f == AuxField::Secure && value != 0)
      return EntryError::Secure;
    seen |= bit;
    state.values[index] = value;
  }
  if (seen != (1U << static_cast<unsigned>(AuxField::Count)) - 1)
    return EntryError::Missing;
  Address count = state.value(AuxField::Phnum);
  if (state.value(AuxField::Phent) != 56 || count == 0 || count >= PN_XNUM ||
      (state.value(AuxField::Phdr) & 7) ||
      !is_user_range(state.value(AuxField::Phdr), count * 56))
    return EntryError::ProgramHeaders;
  Address page = state.value(AuxField::Pagesz);
  if (!is_supported_page_size(page) ||
      (state.value(AuxField::Base) & (page - 1)) ||
      !is_user_range(state.value(AuxField::Base), 64) ||
      !is_aligned_entry(state.value(AuxField::Entry)) ||
      !is_user_range(state.value(AuxField::Entry), 4) ||
      !is_user_range(state.value(AuxField::Random), 16))
    return EntryError::AddressRange;
  for (unsigned i = static_cast<unsigned>(AuxField::Uid);
       i < static_cast<unsigned>(AuxField::Count); ++i)
    if (state.values[i] > 0xffffffff)
      return EntryError::Auxv;
  state.valid = true;
  __builtin_memcpy_inline(&out, &state, sizeof(state));
  return EntryError::None;
}

EntryError locate_main_image(const InitialStack &stack, Bytes phdrs,
                             MainLocation &out) {
  out.valid = false;
  if (!stack.valid)
    return EntryError::Stack;
  Address count = stack.value(AuxField::Phnum);
  if (!phdrs.data() || phdrs.size() != count * 56)
    return EntryError::ProgramHeaders;
  MainLocation location;
  bool found = false;
  for (Address i = 0; i < count; ++i) {
    Elf64_Phdr p;
    if (!decode_program_header(phdrs.subspan(i * 56, 56), stack.big_endian, p))
      return EntryError::ProgramHeaders;
    if (p.p_type != PT_PHDR)
      continue;
    if (found || p.p_filesz < phdrs.size() || p.p_memsz < p.p_filesz ||
        p.p_vaddr > stack.value(AuxField::Phdr))
      return EntryError::ProgramHeaders;
    found = true;
    location.table_vaddr = p.p_vaddr;
    location.bias = stack.value(AuxField::Phdr) - p.p_vaddr;
  }
  if (!found || (location.bias & (stack.value(AuxField::Pagesz) - 1)))
    return EntryError::ProgramHeaders;
  for (Address i = 0; i < count; ++i) {
    Elf64_Phdr p;
    if (!decode_program_header(phdrs.subspan(i * 56, 56), stack.big_endian, p))
      return EntryError::ProgramHeaders;
    if (p.p_type != PT_LOAD || p.p_offset != 0 || p.p_filesz < 64 ||
        p.p_memsz < p.p_filesz || !(p.p_flags & PF_R))
      continue;
    location.header_vaddr = p.p_vaddr;
    if (__builtin_add_overflow(location.bias, p.p_vaddr,
                               &location.header_runtime) ||
        !is_user_range(location.header_runtime, 64))
      return EntryError::AddressRange;
    location.valid = true;
    __builtin_memcpy_inline(&out, &location, sizeof(location));
    return EntryError::None;
  }
  return EntryError::ProgramHeaders;
}

EntryError validate_entry_images(const InitialStack &stack,
                                 const MainLocation &location,
                                 const ElfImage &self, const ElfImage &main,
                                 Address self_pc, const MappedRegion &random) {
  if (!stack.valid || !location.valid)
    return EntryError::Stack;
  const Elf64_Ehdr *sh = self.header(), *mh = main.header();
  if (!sh || !mh || !self.is_mapped() || !main.is_mapped() ||
      self.role() != ImageRole::Interpreter ||
      (main.role() != ImageRole::MainExecutable &&
       main.role() != ImageRole::MainPIE) ||
      main.load_bias() != location.bias ||
      main.header_address() != location.header_vaddr ||
      main.table_address() != location.table_vaddr ||
      self.load_bias() != stack.value(AuxField::Base) ||
      stack.big_endian != (sh->e_ident[EI_DATA] == ELFDATA2MSB) ||
      sh->e_ident[EI_DATA] != mh->e_ident[EI_DATA] ||
      mh->e_phnum != stack.value(AuxField::Phnum))
    return EntryError::Image;
  Address address;
  if (!self.runtime_address(0, 64, PF_R, address) ||
      address != stack.value(AuxField::Base) ||
      !self.runtime_address(self.table_address(), Address(sh->e_phnum) * 56,
                            PF_R, address) ||
      !main.runtime_address(location.header_vaddr, 64, PF_R, address) ||
      address != location.header_runtime ||
      !main.runtime_address(location.table_vaddr, Address(mh->e_phnum) * 56,
                            PF_R, address) ||
      address != stack.value(AuxField::Phdr))
    return EntryError::ProgramHeaders;
  // Recheck discovery against the admitted PHDR, not just an arbitrary readable
  // span at the auxv address.
  bool found = false;
  for (unsigned i = 0; i < mh->e_phnum; ++i) {
    Elf64_Phdr p;
    if (!main.program_header(i, p))
      return EntryError::ProgramHeaders;
    if (p.p_type == PT_PHDR) {
      found = p.p_vaddr == location.table_vaddr;
      break;
    }
  }
  if (!found)
    return EntryError::ProgramHeaders;
  // Neither the kernel's independent mappings nor a synthetic input view may
  // assign a load page to both images.
  Address page = stack.value(AuxField::Pagesz);
  for (unsigned i = 0; i < sh->e_phnum; ++i) {
    Elf64_Phdr s;
    if (!self.program_header(i, s))
      return EntryError::Image;
    if (s.p_type != PT_LOAD || !s.p_memsz)
      continue;
    Address sb;
    if (!self.runtime_address(s.p_vaddr, s.p_memsz, PF_R, sb))
      return EntryError::Image;
    Address se = (sb + s.p_memsz + page - 1) & ~(page - 1);
    sb &= ~(page - 1);
    for (unsigned j = 0; j < mh->e_phnum; ++j) {
      Elf64_Phdr m;
      if (!main.program_header(j, m))
        return EntryError::Image;
      if (m.p_type != PT_LOAD || !m.p_memsz)
        continue;
      Address mb;
      if (!main.runtime_address(m.p_vaddr, m.p_memsz, PF_R, mb))
        return EntryError::Image;
      Address me = (mb + m.p_memsz + page - 1) & ~(page - 1);
      mb &= ~(page - 1);
      if (sb < me && mb < se)
        return EntryError::Image;
    }
  }
  if (!self.runtime_address(sh->e_entry, 4, PF_X, address) ||
      address != self_pc ||
      !main.runtime_address(mh->e_entry, 4, PF_X, address) ||
      address != stack.value(AuxField::Entry))
    return EntryError::Entry;
  Address at_random = stack.value(AuxField::Random);
  if (!random.bytes.data() ||
      !is_user_range(random.vaddr, random.bytes.size()) ||
      at_random < random.vaddr ||
      at_random - random.vaddr > random.bytes.size() ||
      random.bytes.size() - (at_random - random.vaddr) < 16)
    return EntryError::Random;
  return EntryError::None;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
