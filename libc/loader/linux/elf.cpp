//===-- Checked loader ELF views ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/elf.h"
#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool add(Address a, Address b, Address &out) {
  return !__builtin_add_overflow(a, b, &out);
}

static bool contains(Address start, Address length, Address at, Address size) {
  return at >= start && at - start <= length && size <= length - (at - start);
}

static Bytes slice(Bytes bytes, Address offset, Address size) {
  if (!contains(0, bytes.size(), offset, size) || !bytes.data())
    return {};
  return bytes.subspan(offset, size);
}

static Address integer(const unsigned char *p, unsigned size, bool big) {
  Address value = 0;
  for (unsigned i = 0; i < size; ++i)
    value = (value << 8) | p[big ? i : size - i - 1];
  return value;
}

bool decode_elf_header(Bytes bytes, Elf64_Ehdr &h) {
  if (!bytes.data() || bytes.size() < 64)
    return false;
  const auto *p = bytes.data();
  if (p[0] != ELFMAG0 || p[1] != ELFMAG1 || p[2] != ELFMAG2 ||
      p[3] != ELFMAG3 || p[EI_CLASS] != ELFCLASS64 ||
      (p[EI_DATA] != ELFDATA2MSB && p[EI_DATA] != ELFDATA2LSB) ||
      p[EI_VERSION] != EV_CURRENT)
    return false;
  bool big = p[EI_DATA] == ELFDATA2MSB;
  for (unsigned i = 0; i < EI_NIDENT; ++i)
    h.e_ident[i] = p[i];
  h.e_type = static_cast<Elf64_Half>(integer(p + 16, 2, big));
  h.e_machine = static_cast<Elf64_Half>(integer(p + 18, 2, big));
  h.e_version = static_cast<Elf64_Word>(integer(p + 20, 4, big));
  h.e_entry = integer(p + 24, 8, big);
  h.e_phoff = integer(p + 32, 8, big);
  h.e_shoff = integer(p + 40, 8, big);
  h.e_flags = static_cast<Elf64_Word>(integer(p + 48, 4, big));
  h.e_ehsize = static_cast<Elf64_Half>(integer(p + 52, 2, big));
  h.e_phentsize = static_cast<Elf64_Half>(integer(p + 54, 2, big));
  h.e_phnum = static_cast<Elf64_Half>(integer(p + 56, 2, big));
  h.e_shentsize = static_cast<Elf64_Half>(integer(p + 58, 2, big));
  h.e_shnum = static_cast<Elf64_Half>(integer(p + 60, 2, big));
  h.e_shstrndx = static_cast<Elf64_Half>(integer(p + 62, 2, big));
  Address end;
  return h.e_version == EV_CURRENT && h.e_ehsize == 64 && h.e_phentsize == 56 &&
         h.e_phnum != 0 && h.e_phnum != PN_XNUM && h.e_phoff >= 64 &&
         add(h.e_phoff, Address(h.e_phnum) * 56, end);
}

bool decode_program_header(Bytes bytes, bool big, Elf64_Phdr &h) {
  if (!bytes.data() || bytes.size() < 56)
    return false;
  const auto *p = bytes.data();
  h.p_type = static_cast<Elf64_Word>(integer(p, 4, big));
  h.p_flags = static_cast<Elf64_Word>(integer(p + 4, 4, big));
  h.p_offset = integer(p + 8, 8, big);
  h.p_vaddr = integer(p + 16, 8, big);
  h.p_paddr = integer(p + 24, 8, big);
  h.p_filesz = integer(p + 32, 8, big);
  h.p_memsz = integer(p + 40, 8, big);
  h.p_align = integer(p + 48, 8, big);
  return true;
}

Bytes ElfImage::file_backed_tail(Address vaddr) const {
  if (!admitted)
    return {};
  for (unsigned i = 0; i < ehdr.e_phnum; ++i) {
    Elf64_Phdr p;
    if (!read_program_header(i, p))
      return {};
    if (p.p_type != PT_LOAD || !(p.p_flags & PF_R) ||
        !contains(p.p_vaddr, p.p_filesz, vaddr, 1))
      continue;
    Address size = p.p_filesz - (vaddr - p.p_vaddr);
    Address offset = p.p_offset + (vaddr - p.p_vaddr);
    for (unsigned j = i + 1; j < ehdr.e_phnum; ++j) {
      Elf64_Phdr next;
      if (!read_program_header(j, next))
        return {};
      if (next.p_type != PT_LOAD || !next.p_memsz)
        continue;
      if (!(next.p_flags & PF_R) || next.p_vaddr != vaddr + size ||
          (!mapped && next.p_offset != offset + size))
        break;
      size += next.p_filesz;
    }
    if (mapped) {
      for (const auto &r : regions) {
        if (!contains(r.vaddr, r.bytes.size(), vaddr, 1))
          continue;
        Address available = r.bytes.size() - (vaddr - r.vaddr);
        if (size > available)
          size = available;
        return read_file_bytes(vaddr, size);
      }
      return {};
    }
    return read_file_bytes(vaddr, size);
  }
  return {};
}

bool ElfImage::read_program_header(unsigned index, Elf64_Phdr &out) const {
  return index < ehdr.e_phnum &&
         decode_program_header(slice(phdrs, Address(index) * 56, 56),
                               ehdr.e_ident[EI_DATA] == ELFDATA2MSB, out);
}

Bytes ElfImage::mapped_bytes(Address address, Address size) const {
  for (const auto &r : regions)
    if (contains(r.vaddr, r.bytes.size(), address, size))
      return slice(r.bytes, address - r.vaddr, size);
  return {};
}

// LOADs are validated in ascending, page-disjoint order before coverage
// queries. Contiguous segments may jointly cover metadata, but holes and BSS
// may not stand in for file-backed bytes.
bool ElfImage::coverage(Address address, Address size, unsigned flags,
                        bool file_backed, Address offset,
                        bool check_offset) const {
  Address end;
  if (!add(address, size, end))
    return false;
  for (unsigned i = 0; i < ehdr.e_phnum && address < end; ++i) {
    Elf64_Phdr p;
    if (!read_program_header(i, p))
      return false;
    Address length = file_backed ? p.p_filesz : p.p_memsz;
    if (p.p_type != PT_LOAD || (p.p_flags & flags) != flags ||
        !contains(p.p_vaddr, length, address, 1))
      continue;
    Address delta = address - p.p_vaddr;
    if (check_offset && p.p_offset + delta != offset)
      return false;
    Address count = length - delta;
    if (count > end - address)
      count = end - address;
    address += count;
    if (check_offset && !add(offset, count, offset))
      return false;
  }
  return address == end;
}

bool ElfImage::find_file_address(Address offset, Address size,
                                 Address &address) const {
  for (unsigned i = 0; i < ehdr.e_phnum; ++i) {
    Elf64_Phdr p;
    if (!read_program_header(i, p))
      return false;
    if (p.p_type == PT_LOAD && contains(p.p_offset, p.p_filesz, offset, 1)) {
      Address v = p.p_vaddr + (offset - p.p_offset);
      if (coverage(v, size, PF_R, true, offset, true)) {
        address = v;
        return true;
      }
    }
  }
  return false;
}

Bytes ElfImage::read_file_bytes(Address address, Address size) const {
  if (!coverage(address, size, PF_R, true, 0, false))
    return {};
  if (mapped)
    return mapped_bytes(address, size);
  for (unsigned i = 0; i < ehdr.e_phnum; ++i) {
    Elf64_Phdr p;
    if (!read_program_header(i, p))
      return {};
    if (p.p_type == PT_LOAD && contains(p.p_vaddr, p.p_filesz, address, 1)) {
      Address offset = p.p_offset + (address - p.p_vaddr);
      if (coverage(address, size, PF_R, true, offset, true))
        return slice(file, offset, size);
    }
  }
  return {};
}

bool ElfImage::runtime_address(Address address, Address size, unsigned flags,
                               Address &out) const {
  Address runtime;
  if (!admitted || !add(bias, address, runtime) ||
      !is_user_range(runtime, size) ||
      !coverage(address, size, flags, false, 0, false))
    return false;
  out = runtime;
  return true;
}

ElfError ElfImage::open_file(Bytes bytes, ImageRole role, Address load_bias,
                             Address page_size, ElfImage &out) {
  out.admitted = false;
  ElfImage image;
  if (!decode_elf_header(bytes, image.ehdr))
    return ElfError::Header;
  image.file = bytes;
  image.phdrs =
      slice(bytes, image.ehdr.e_phoff, Address(image.ehdr.e_phnum) * 56);
  if (image.phdrs.empty())
    return ElfError::Truncated;
  image.bias = load_bias;
  ElfError error = image.validate(role, page_size);
  if (error == ElfError::None) {
    image.admitted = true;
    image.image_role = role;
    out = image;
  }
  return error;
}

ElfError ElfImage::open_mapped(cpp::span<const MappedRegion> mappings,
                               Address header_address, Address table_address,
                               ImageRole role, Address load_bias,
                               Address page_size, ElfImage &out) {
  out.admitted = false;
  ElfImage image;
  image.mapped = true;
  image.regions = mappings;
  image.header_vaddr = header_address;
  image.table_vaddr = table_address;
  image.bias = load_bias;
  Address previous_end = 0;
  for (const auto &r : mappings) {
    Address end;
    if (!r.bytes.data() || r.bytes.empty() || r.vaddr < previous_end ||
        !add(r.vaddr, r.bytes.size(), end))
      return ElfError::AddressRange;
    previous_end = end;
  }
  if (!decode_elf_header(image.mapped_bytes(header_address, 64), image.ehdr))
    return ElfError::Header;
  image.phdrs =
      image.mapped_bytes(table_address, Address(image.ehdr.e_phnum) * 56);
  if (image.phdrs.empty())
    return ElfError::Truncated;
  ElfError error = image.validate(role, page_size);
  if (error == ElfError::None) {
    image.admitted = true;
    image.image_role = role;
    out = image;
  }
  return error;
}

ElfError ElfImage::validate(ImageRole role, Address page_size) {
  if (!is_supported_target(ehdr) || !is_supported_page_size(page_size))
    return ElfError::Target;
  if (ehdr.e_type != (role == ImageRole::MainExecutable ? ET_EXEC : ET_DYN) ||
      (ehdr.e_type == ET_EXEC && bias != 0) || (bias & (page_size - 1)))
    return ElfError::AddressRange;
  unsigned loads = 0, stacks = 0, tables = 0, interps = 0, dynamics = 0,
           relros = 0;
  Elf64_Phdr table, interp, dynamic, relro;
  Address previous_end = 0;
  for (unsigned i = 0; i < ehdr.e_phnum; ++i) {
    Elf64_Phdr p;
    if (!read_program_header(i, p))
      return ElfError::Truncated;
    if (p.p_type == PT_LOAD) {
      Address file_end, end, runtime, rounded_end;
      if (p.p_filesz > p.p_memsz || !add(p.p_offset, p.p_filesz, file_end) ||
          (!mapped && file_end > file.size()) ||
          !add(p.p_vaddr, p.p_memsz, end) ||
          !add(end, page_size - 1, rounded_end) ||
          !add(bias, p.p_vaddr & ~(page_size - 1), runtime))
        return ElfError::Load;
      rounded_end &= ~(page_size - 1);
      Address start = p.p_vaddr & ~(page_size - 1);
      if (p.p_memsz && !is_user_range(runtime, rounded_end - start))
        return ElfError::AddressRange;
      if ((p.p_flags & ~(PF_R | PF_W | PF_X)) || !(p.p_flags & PF_R) ||
          ((p.p_flags & PF_W) && (p.p_flags & PF_X)) ||
          ((p.p_vaddr ^ p.p_offset) & (page_size - 1)))
        return ElfError::Load;
      if (p.p_align > 1 &&
          ((p.p_align & (p.p_align - 1)) ||
           ((p.p_vaddr ^ p.p_offset) & (p.p_align - 1)) ||
           (bias & (p.p_align - 1)) ||
           (role == ImageRole::Interpreter && p.p_align > page_size)))
        return ElfError::Load;
      if (p.p_memsz) {
        if (loads && start < previous_end)
          return ElfError::Overlap;
        previous_end = rounded_end;
        ++loads;
      }
    } else if (p.p_type == PT_GNU_STACK) {
      if (++stacks != 1 || p.p_flags != (PF_R | PF_W))
        return ElfError::Stack;
    } else if (p.p_type == PT_PHDR) {
      if (++tables != 1)
        return ElfError::ProgramHeaders;
      __builtin_memcpy_inline(&table, &p, sizeof(p));
    } else if (p.p_type == PT_INTERP) {
      if (++interps != 1)
        return ElfError::Interpreter;
      __builtin_memcpy_inline(&interp, &p, sizeof(p));
    } else if (p.p_type == PT_DYNAMIC) {
      if (++dynamics != 1)
        return ElfError::Dynamic;
      __builtin_memcpy_inline(&dynamic, &p, sizeof(p));
    } else if (p.p_type == PT_GNU_RELRO) {
      if (++relros != 1)
        return ElfError::Relro;
      __builtin_memcpy_inline(&relro, &p, sizeof(p));
    }
  }
  bool main = role == ImageRole::MainExecutable || role == ImageRole::MainPIE;
  if (!loads || stacks != 1)
    return ElfError::Stack;
  Address table_size = Address(ehdr.e_phnum) * 56;
  if (!mapped) {
    if (!find_file_address(0, 64, header_vaddr))
      return ElfError::ProgramHeaders;
    if (tables)
      table_vaddr = table.p_vaddr;
    else if (!find_file_address(ehdr.e_phoff, table_size, table_vaddr))
      return ElfError::ProgramHeaders;
  }
  if (!coverage(header_vaddr, 64, PF_R, true, 0, true) ||
      !coverage(table_vaddr, table_size, PF_R, true, ehdr.e_phoff, true) ||
      (role == ImageRole::Interpreter && header_vaddr != 0) ||
      (role != ImageRole::SharedObject && tables != 1))
    return ElfError::ProgramHeaders;
  if (tables &&
      (table.p_offset != ehdr.e_phoff || table.p_vaddr != table_vaddr ||
       table.p_filesz < table_size || table.p_memsz < table.p_filesz ||
       !coverage(table.p_vaddr, table.p_filesz, PF_R, true, table.p_offset,
                 true) ||
       !coverage(table.p_vaddr, table.p_memsz, PF_R, false, 0, false)))
    return ElfError::ProgramHeaders;
  if (interps != (main ? 1U : 0U))
    return ElfError::Interpreter;
  if (main) {
    const char *path = interpreter_path();
    Address length = 0;
    while (path[length])
      ++length;
    if (interp.p_filesz != length + 1 || interp.p_memsz < interp.p_filesz ||
        !coverage(interp.p_vaddr, interp.p_filesz, PF_R, true, interp.p_offset,
                  true))
      return ElfError::Interpreter;
    Bytes name = read_file_bytes(interp.p_vaddr, interp.p_filesz);
    if (name.size() != length + 1)
      return ElfError::Interpreter;
    for (Address i = 0; i <= length; ++i)
      if (name[i] != static_cast<unsigned char>(path[i]))
        return ElfError::Interpreter;
  }
  if (dynamics != 1 || dynamic.p_filesz == 0 || dynamic.p_filesz % 16 ||
      dynamic.p_filesz > dynamic.p_memsz ||
      !coverage(dynamic.p_vaddr, dynamic.p_filesz, PF_R, true, dynamic.p_offset,
                true) ||
      !coverage(dynamic.p_vaddr, dynamic.p_memsz, PF_R, false, 0, false))
    return ElfError::Dynamic;
  Bytes dyn = read_file_bytes(dynamic.p_vaddr, dynamic.p_filesz);
  if (dyn.size() != dynamic.p_filesz)
    return ElfError::Dynamic;
  bool terminated = false;
  for (Address i = 0; i < dyn.size(); i += 16) {
    Address tag =
        integer(dyn.data() + i, 8, ehdr.e_ident[EI_DATA] == ELFDATA2MSB);
    if (tag == DT_NULL) {
      terminated = true;
      break;
    }
    if (role == ImageRole::Interpreter && tag == DT_NEEDED)
      return ElfError::Dynamic;
  }
  if (!terminated)
    return ElfError::Dynamic;
  Address relro_end;
  if (relros && (!add(relro.p_vaddr, relro.p_memsz, relro_end) ||
                 (relro.p_memsz && ((relro.p_vaddr & (page_size - 1)) ||
                                    (relro_end & (page_size - 1)) ||
                                    !coverage(relro.p_vaddr, relro.p_memsz,
                                              PF_R | PF_W, false, 0, false)))))
    return ElfError::Relro;
  Address entry;
  if ((role != ImageRole::SharedObject || ehdr.e_entry != 0) &&
      (!add(bias, ehdr.e_entry, entry) || !is_aligned_entry(entry) ||
       !is_user_range(entry, 4) ||
       !coverage(ehdr.e_entry, 4, PF_R | PF_X, false, 0, false)))
    return ElfError::Entry;
  return ElfError::None;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
