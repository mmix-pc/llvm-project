//===-- Loader RELATIVE-only self relocation ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/self_relocation.h"
#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static Address octa(const unsigned char *p, bool big) {
  Address value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value = (value << 8) | p[big ? i : 7 - i];
  return value;
}

static bool overlap(Address a, Address n, Address b, Address m) {
  return n && m && (a <= b ? b - a < n : a - b < m);
}

static unsigned char *destination(const ElfImage &image,
                                  cpp::span<const WritableRegion> writable,
                                  Address offset) {
  Bytes expected = image.mapped_memory_bytes(offset, 8, PF_R | PF_W);
  if (expected.size() != 8)
    return nullptr;
  for (const auto &r : writable) {
    if (!r.bytes.data() || offset < r.vaddr ||
        offset - r.vaddr > r.bytes.size() ||
        r.bytes.size() - (offset - r.vaddr) < 8)
      continue;
    unsigned char *p = r.bytes.data() + (offset - r.vaddr);
    if (p == expected.data())
      return p;
  }
  return nullptr;
}

static bool relative_value(Address base, Address addend, Address &out) {
  if (addend >> 63) {
    Address magnitude = ~addend + 1;
    if (base < magnitude)
      return false;
    out = base - magnitude;
  } else if (__builtin_add_overflow(base, addend, &out)) {
    return false;
  }
  return is_user_range(out, 1);
}

RelocationError relocate_self(const ElfImage &image,
                              cpp::span<const WritableRegion> writable) {
  const Elf64_Ehdr *h = image.header();
  if (!h || !image.is_mapped() || image.role() != ImageRole::Interpreter)
    return RelocationError::Image;
  Elf64_Phdr dynamic;
  bool have_dynamic = false;
  for (unsigned i = 0; i < h->e_phnum; ++i) {
    Elf64_Phdr p;
    if (!image.program_header(i, p))
      return RelocationError::Image;
    if (p.p_type == PT_TLS)
      return RelocationError::Unsupported;
    if (p.p_type == PT_DYNAMIC) {
      __builtin_memcpy_inline(&dynamic, &p, sizeof(p));
      have_dynamic = true;
    }
  }
  if (!have_dynamic)
    return RelocationError::Dynamic;
  Bytes dyn = image.file_backed_bytes(dynamic.p_vaddr, dynamic.p_filesz);
  if (dyn.empty())
    return RelocationError::Dynamic;
  enum Field {
    Rela,
    Relasz,
    Relaent,
    Relacount,
    Strtab,
    Strsz,
    Symtab,
    Syment,
    Hash,
    GnuHash,
    Soname,
    Flags,
    Flags1,
    BindNow,
    Checksum,
    Count
  };
  Address values[Count];
  unsigned seen = 0;
  bool big = h->e_ident[EI_DATA] == ELFDATA2MSB;
  bool terminated = false;
  for (Address i = 0; i < dyn.size(); i += 16) {
    Address tag = octa(dyn.data() + i, big);
    if (tag == DT_NULL) {
      terminated = true;
      break;
    }
    Field f;
    switch (tag) {
    case DT_RELA:
      f = Rela;
      break;
    case DT_RELASZ:
      f = Relasz;
      break;
    case DT_RELAENT:
      f = Relaent;
      break;
    case DT_RELACOUNT:
      f = Relacount;
      break;
    case DT_STRTAB:
      f = Strtab;
      break;
    case DT_STRSZ:
      f = Strsz;
      break;
    case DT_SYMTAB:
      f = Symtab;
      break;
    case DT_SYMENT:
      f = Syment;
      break;
    case DT_HASH:
      f = Hash;
      break;
    case DT_GNU_HASH:
      f = GnuHash;
      break;
    case DT_SONAME:
      f = Soname;
      break;
    case DT_FLAGS:
      f = Flags;
      break;
    case DT_FLAGS_1:
      f = Flags1;
      break;
    case DT_BIND_NOW:
      f = BindNow;
      break;
    case DT_CHECKSUM:
      f = Checksum;
      break;
    // No dependencies, TLS, symbol lookup, PLT, constructors or unknown
    // semantics are permitted in the interpreter's own bootstrap metadata.
    default:
      return RelocationError::Unsupported;
    }
    if (seen & (1U << f))
      return RelocationError::Dynamic;
    seen |= 1U << f;
    values[f] = octa(dyn.data() + i + 8, big);
  }
  if (!terminated)
    return RelocationError::Dynamic;
  if (((seen & (1U << Flags)) && (values[Flags] & ~Address(DF_BIND_NOW))) ||
      ((seen & (1U << Flags1)) && (values[Flags1] & ~Address(DF_1_NOW))))
    return RelocationError::Unsupported;
  auto has = [seen](Field f) { return (seen & (1U << f)) != 0; };
  if (has(Strtab) != has(Strsz) || has(Symtab) != has(Syment) ||
      (has(Strtab) &&
       (values[Strsz] == 0 ||
        image.file_backed_bytes(values[Strtab], values[Strsz]).size() !=
            values[Strsz])) ||
      (has(Symtab) &&
       (values[Syment] != 24 ||
        image.file_backed_bytes(values[Symtab], 24).size() != 24)) ||
      (has(Hash) && image.file_backed_bytes(values[Hash], 8).size() != 8) ||
      (has(GnuHash) &&
       image.file_backed_bytes(values[GnuHash], 16).size() != 16))
    return RelocationError::Table;
  if (has(Soname)) {
    if (!has(Strtab) || values[Soname] >= values[Strsz])
      return RelocationError::Table;
    Bytes strings = image.file_backed_bytes(values[Strtab], values[Strsz]);
    Address end = values[Soname];
    while (end < strings.size() && strings[end])
      ++end;
    if (end == strings.size())
      return RelocationError::Table;
  }
  unsigned rela_fields = (1U << Rela) | (1U << Relasz) | (1U << Relaent);
  if (!(seen & rela_fields))
    return has(Relacount) ? RelocationError::Table : RelocationError::None;
  if ((seen & rela_fields) != rela_fields || values[Relaent] != 24 ||
      values[Relasz] % 24 ||
      (has(Relacount) && values[Relacount] > values[Relasz] / 24))
    return RelocationError::Table;
  Bytes relas = image.file_backed_bytes(values[Rela], values[Relasz]);
  if (relas.size() != values[Relasz])
    return RelocationError::Table;
  for (Address i = 0; i < relas.size(); i += 24) {
    Address offset = octa(relas.data() + i, big), value;
    if (!is_relative_relocation(octa(relas.data() + i + 8, big)))
      return RelocationError::Unsupported;
    if (!destination(image, writable, offset))
      return RelocationError::Destination;
    if (!relative_value(image.load_bias(), octa(relas.data() + i + 16, big),
                        value))
      return RelocationError::Value;
    if (overlap(offset, 8, image.header_address(), 64) ||
        overlap(offset, 8, image.table_address(), Address(h->e_phnum) * 56) ||
        overlap(offset, 8, dynamic.p_vaddr, dynamic.p_memsz) ||
        overlap(offset, 8, values[Rela], values[Relasz]))
      return RelocationError::Overlap;
    for (Address j = 0; j < i; j += 24)
      if (overlap(offset, 8, octa(relas.data() + j, big), 8))
        return RelocationError::Overlap;
  }
  // The table and all validation inputs remain unchanged during this pass.
  for (Address i = 0; i < relas.size(); i += 24) {
    Address value;
    (void)relative_value(image.load_bias(), octa(relas.data() + i + 16, big),
                         value);
    unsigned char *p =
        destination(image, writable, octa(relas.data() + i, big));
    for (unsigned j = 0; j < 8; ++j) {
      p[big ? 7 - j : j] = static_cast<unsigned char>(value);
      value >>= 8;
    }
  }
  return RelocationError::None;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
