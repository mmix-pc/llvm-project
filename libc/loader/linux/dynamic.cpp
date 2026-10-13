//===-- Checked dynamic metadata -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/dynamic.h"
#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Keep construction beside clear so redundant member initialization cannot
// become an external memset when this view is embedded in an allocated node.
DynamicInfo::DynamicInfo() { clear(); }

static Address integer(const unsigned char *p, unsigned width, bool big) {
  Address value = 0;
  for (unsigned i = 0; i < width; ++i)
    value = (value << 8) | p[big ? i : width - 1 - i];
  return value;
}

static unsigned slot(DynamicTag tag) { return static_cast<unsigned>(tag); }

static bool overlaps(Address destination, Address start, Address size) {
  return size && (destination <= start ? start - destination < 8
                                       : destination - start < size);
}

static bool tag_field(Address tag, DynamicTag &out) {
  switch (tag) {
#define FIELD(ELF, Field)                                                      \
  case ELF:                                                                    \
    out = DynamicTag::Field;                                                   \
    return true
    FIELD(DT_STRTAB, Strtab);
    FIELD(DT_STRSZ, Strsz);
    FIELD(DT_SYMTAB, Symtab);
    FIELD(DT_SYMENT, Syment);
    FIELD(DT_HASH, Hash);
    FIELD(DT_GNU_HASH, GnuHash);
    FIELD(DT_SONAME, Soname);
    FIELD(DT_RPATH, Rpath);
    FIELD(DT_RUNPATH, Runpath);
    FIELD(DT_RELA, Rela);
    FIELD(DT_RELASZ, Relasz);
    FIELD(DT_RELAENT, Relaent);
    FIELD(DT_RELACOUNT, Relacount);
    FIELD(DT_INIT, Init);
    FIELD(DT_FINI, Fini);
    FIELD(DT_INIT_ARRAY, InitArray);
    FIELD(DT_INIT_ARRAYSZ, InitArraySize);
    FIELD(DT_FINI_ARRAY, FiniArray);
    FIELD(DT_FINI_ARRAYSZ, FiniArraySize);
    FIELD(DT_PREINIT_ARRAY, PreinitArray);
    FIELD(DT_PREINIT_ARRAYSZ, PreinitArraySize);
    FIELD(DT_FLAGS, Flags);
    FIELD(DT_FLAGS_1, Flags1);
    FIELD(DT_BIND_NOW, BindNow);
    FIELD(DT_DEBUG, Debug);
    FIELD(DT_CHECKSUM, Checksum);
#undef FIELD
  default:
    return false;
  }
}

void DynamicInfo::clear() {
  // All supported targets use all-zero null pointers. Keep the complete reset
  // inline: adjacent span resets otherwise combine into a libc memset call.
  static_assert(__is_trivially_copyable(DynamicInfo));
  __builtin_memset_inline(this, 0, sizeof(*this));
  big = true;
}

bool DynamicInfo::read_string(Address offset, Bytes &out) const {
  out = {};
  if (offset >= strings.size())
    return false;
  Address end = offset;
  while (end < strings.size() && strings[end])
    ++end;
  if (end == strings.size())
    return false;
  out = strings.subspan(offset, end - offset);
  return true;
}

bool DynamicInfo::read_symbol(Address index, Elf64_Sym &out) const {
  if (index >= symbols.size() / 24)
    return false;
  const auto *p = symbols.data() + index * 24;
  out.st_name = static_cast<Elf64_Word>(integer(p, 4, big));
  out.st_info = p[4];
  out.st_other = p[5];
  out.st_shndx = static_cast<Elf64_Half>(integer(p + 6, 2, big));
  out.st_value = integer(p + 8, 8, big);
  out.st_size = integer(p + 16, 8, big);
  return true;
}

bool DynamicInfo::read_relocation(Address index, DynamicRelocation &out) const {
  if (index >= relas.size() / 24)
    return false;
  const auto *p = relas.data() + index * 24;
  out.offset = integer(p, 8, big);
  out.info = integer(p + 8, 8, big);
  out.addend = integer(p + 16, 8, big);
  return true;
}

bool DynamicInfo::needed(Address index, Bytes &out) const {
  out = {};
  if (!valid || index >= needed_size)
    return false;
  for (Address i = 0; i < dynamic.size(); i += 16)
    if (integer(dynamic.data() + i, 8, big) == DT_NEEDED && index-- == 0)
      return read_string(integer(dynamic.data() + i + 8, 8, big), out);
  return false;
}

Bytes DynamicInfo::lifecycle_array(DynamicTag tag) const {
  if (!valid)
    return {};
  switch (tag) {
  case DynamicTag::InitArray:
    return arrays[0];
  case DynamicTag::FiniArray:
    return arrays[1];
  case DynamicTag::PreinitArray:
    return arrays[2];
  default:
    return {};
  }
}

static Elf64_Word name_hash(Bytes name, bool gnu) {
  Elf64_Word hash = gnu ? 5381 : 0;
  for (unsigned char c : name) {
    if (gnu) {
      hash = hash * 33 + c;
    } else {
      hash = (hash << 4) + c;
      Elf64_Word high = hash & 0xf0000000;
      hash ^= high >> 24;
      hash &= ~high;
    }
  }
  return hash;
}

static bool lookup_definition(const Elf64_Sym &symbol) {
  return (symbol.st_info >> 4) != STB_LOCAL && symbol.st_shndx != SHN_UNDEF &&
         (symbol.st_other == STV_DEFAULT || symbol.st_other == STV_PROTECTED);
}

bool DynamicInfo::find_definition(Bytes name, Address &out) const {
  out = 0;
  if (!valid || name.empty())
    return false;
  auto matches = [&](Address index) {
    Elf64_Sym symbol;
    Bytes candidate;
    if (!read_symbol(index, symbol) || !lookup_definition(symbol) ||
        !read_string(symbol.st_name, candidate) ||
        candidate.size() != name.size())
      return false;
    for (size_t i = 0; i < name.size(); ++i)
      if (candidate[i] != name[i])
        return false;
    out = index;
    return true;
  };
  if (!gnu.empty()) {
    Address buckets = integer(gnu.data(), 4, big);
    Address start = integer(gnu.data() + 4, 4, big);
    Address blooms = integer(gnu.data() + 8, 4, big);
    Address shift = integer(gnu.data() + 12, 4, big);
    Elf64_Word hash = name_hash(name, true);
    Address bloom = integer(gnu.data() + 16 + (hash / 64 % blooms) * 8, 8, big);
    Address mask =
        (Address(1) << (hash % 64)) | (Address(1) << ((hash >> shift) % 64));
    if ((bloom & mask) != mask)
      return false;
    Address table = 16 + blooms * 8;
    Address index = integer(gnu.data() + table + (hash % buckets) * 4, 4, big);
    if (index < start)
      return false;
    for (; index < symbol_count(); ++index) {
      Address chain = integer(
          gnu.data() + table + buckets * 4 + (index - start) * 4, 4, big);
      if ((chain | 1) == (Address(hash) | 1) && matches(index))
        return true;
      if (chain & 1)
        break;
    }
    return false;
  }
  Address buckets = integer(sysv.data(), 4, big);
  Address index =
      integer(sysv.data() + 8 + (name_hash(name, false) % buckets) * 4, 4, big);
  for (Address steps = 0; index && steps < symbol_count(); ++steps) {
    if (matches(index))
      return true;
    index = integer(sysv.data() + 8 + (buckets + index) * 4, 4, big);
  }
  return false;
}

DynamicError DynamicInfo::validate_hashes(const ElfImage &image) {
  auto has = [this](DynamicTag t) { return seen & (Address(1) << slot(t)); };
  auto word = [this](Bytes b, Address index) {
    return integer(b.data() + index * 4, 4, big);
  };
  Address symbol_limit = symbols.size() / 24, count = 0;
  if (has(DynamicTag::Hash)) {
    sysv = image.file_backed_tail(values[slot(DynamicTag::Hash)]);
    if (sysv.size() < 8)
      return DynamicError::Hash;
    Address buckets = word(sysv, 0);
    count = word(sysv, 1);
    if (!buckets || !count || count > symbol_limit ||
        2 + buckets + count > sysv.size() / 4)
      return DynamicError::Hash;
    sysv = sysv.first((2 + buckets + count) * 4);
    Address links = 0, reachable_links = 0;
    for (Address i = 0; i < buckets + count; ++i) {
      Address next = word(sysv, i + 2);
      if (next >= count)
        return DynamicError::Hash;
      if (i >= buckets && next)
        ++links;
    }
    if (word(sysv, 2 + buckets) != 0)
      return DynamicError::Hash;
    Address definitions = 0, reachable = 0;
    for (Address i = 1; i < count; ++i) {
      Elf64_Sym symbol;
      if (!read_symbol(i, symbol))
        return DynamicError::Hash;
      definitions += lookup_definition(symbol);
    }
    for (Address b = 0; b < buckets; ++b) {
      Address index = word(sysv, 2 + b), steps = 0;
      while (index) {
        Elf64_Sym symbol;
        Bytes name;
        if (++steps >= count || !read_symbol(index, symbol) ||
            !read_string(symbol.st_name, name) ||
            name_hash(name, false) % buckets != b)
          return DynamicError::Hash;
        reachable += lookup_definition(symbol);
        index = word(sysv, 2 + buckets + index);
        if (index)
          ++reachable_links;
      }
    }
    // A symbol has exactly one hash bucket. Cycles were rejected above, so
    // matching the count also proves no externally visible definition is lost.
    if (reachable != definitions || reachable_links != links)
      return DynamicError::Hash;
  }
  if (has(DynamicTag::GnuHash)) {
    gnu = image.file_backed_tail(values[slot(DynamicTag::GnuHash)]);
    if (gnu.size() < 16)
      return DynamicError::Hash;
    Address buckets = word(gnu, 0), start = word(gnu, 1);
    Address blooms = word(gnu, 2), shift = word(gnu, 3);
    Address bucket_at = 4 + blooms * 2, chain_at = bucket_at + buckets;
    if (!buckets || !blooms || (blooms & (blooms - 1)) || shift >= 32 ||
        !start || start > symbol_limit || (count && start > count) ||
        chain_at > gnu.size() / 4)
      return DynamicError::Hash;
    Address end = start, visited = 0;
    for (Address i = 1; i < start; ++i) {
      Elf64_Sym symbol;
      if (!read_symbol(i, symbol) || lookup_definition(symbol))
        return DynamicError::Hash;
    }
    // GNU chains are contiguous per bucket. Bound every step by both mapped
    // chain bytes and symbol storage; never infer a count from section headers.
    for (Address b = 0; b < buckets; ++b) {
      Address index = word(gnu, bucket_at + b);
      if (!index)
        continue;
      if (index < start)
        return DynamicError::Hash;
      for (;;) {
        if (index >= symbol_limit || (count && index >= count) ||
            index - start >= gnu.size() / 4 - chain_at)
          return DynamicError::Hash;
        Elf64_Sym symbol;
        Bytes name;
        if (!read_symbol(index, symbol) || !read_string(symbol.st_name, name))
          return DynamicError::Hash;
        Elf64_Word hash = name_hash(name, true);
        Address chain = word(gnu, chain_at + index - start);
        Address bloom =
            integer(gnu.data() + 16 + ((hash / 64) % blooms) * 8, 8, big);
        Address mask = (Address(1) << (hash % 64)) |
                       (Address(1) << ((hash >> shift) % 64));
        if ((chain | 1) != (Address(hash) | 1) || hash % buckets != b ||
            (bloom & mask) != mask)
          return DynamicError::Hash;
        ++visited;
        if (++index > end)
          end = index;
        if (chain & 1)
          break;
      }
    }
    if ((count && end != count) || visited != end - start)
      return DynamicError::Hash;
    count = end;
    gnu = gnu.first((chain_at + count - start) * 4);
  }
  if (!count)
    return DynamicError::Hash;
  symbols = symbols.first(count * 24);
  return DynamicError::None;
}

DynamicError DynamicInfo::parse(const ElfImage &image) {
  const auto *header = image.header();
  if (!header)
    return DynamicError::Image;
  big = header->e_ident[EI_DATA] == ELFDATA2MSB;
  Address dynamic_address = 0;
  for (unsigned i = 0; i < header->e_phnum; ++i) {
    Elf64_Phdr p;
    if (!image.program_header(i, p))
      return DynamicError::Image;
    if (p.p_type == PT_TLS)
      return DynamicError::Unsupported;
    if (p.p_type == PT_DYNAMIC) {
      dynamic = image.file_backed_bytes(p.p_vaddr, p.p_filesz);
      dynamic_address = p.p_vaddr;
    }
  }
  bool terminated = false;
  bool main = image.role() == ImageRole::MainExecutable ||
              image.role() == ImageRole::MainPIE;
  for (Address i = 0; i < dynamic.size(); i += 16) {
    Address tag = integer(dynamic.data() + i, 8, big);
    Address value = integer(dynamic.data() + i + 8, 8, big);
    if (tag == DT_NULL) {
      terminated = true;
      break;
    }
    if (tag == DT_NEEDED) {
      ++needed_size;
      continue;
    }
    DynamicTag field;
    if (!tag_field(tag, field))
      return DynamicError::Unsupported;
    Address bit = Address(1) << slot(field);
    if (seen & bit)
      return DynamicError::Duplicate;
    seen |= bit;
    values[slot(field)] = value;
    if (field == DynamicTag::Debug) {
      Address ignored;
      if (!main || value ||
          !image.runtime_address(dynamic_address + i + 8, 8, PF_R | PF_W,
                                 ignored))
        return DynamicError::Unsupported;
    }
  }
  if (!terminated)
    return DynamicError::Table;
  auto has = [this](DynamicTag t) {
    return (seen & (Address(1) << slot(t))) != 0;
  };
  auto val = [this](DynamicTag t) { return values[slot(t)]; };
  if ((val(DynamicTag::Flags) & ~Address(DF_BIND_NOW)) ||
      (val(DynamicTag::Flags1) &
       ~Address(DF_1_NOW | DF_1_PIE | DF_1_NODELETE)) ||
      ((val(DynamicTag::Flags1) & DF_1_PIE) &&
       image.role() != ImageRole::MainPIE))
    return DynamicError::Unsupported;
  if (!has(DynamicTag::Strtab) || !has(DynamicTag::Strsz) ||
      !has(DynamicTag::Symtab) || !has(DynamicTag::Syment) ||
      val(DynamicTag::Syment) != 24 || !val(DynamicTag::Strsz))
    return DynamicError::Table;
  strings =
      image.file_backed_bytes(val(DynamicTag::Strtab), val(DynamicTag::Strsz));
  if (strings.size() != val(DynamicTag::Strsz) || strings.front() ||
      strings.back())
    return DynamicError::String;
  symbols = image.file_backed_tail(val(DynamicTag::Symtab));
  for (Address i = 0; i < dynamic.size(); i += 16) {
    Address tag = integer(dynamic.data() + i, 8, big);
    if (tag == DT_NULL)
      break;
    if (tag == DT_NEEDED || tag == DT_SONAME || tag == DT_RPATH ||
        tag == DT_RUNPATH) {
      Bytes name;
      if (!read_string(integer(dynamic.data() + i + 8, 8, big), name) ||
          (tag == DT_NEEDED && name.empty()))
        return DynamicError::String;
    }
  }
  DynamicError error = validate_hashes(image);
  if (error != DynamicError::None)
    return error;
  for (Address i = 0; i < symbols.size() / 24; ++i) {
    Elf64_Sym s;
    Bytes name;
    if (!read_symbol(i, s) || !read_string(s.st_name, name))
      return DynamicError::Symbol;
    unsigned bind = s.st_info >> 4, type = s.st_info & 15;
    if (bind > STB_WEAK || type > STT_FILE ||
        (type >= STT_SECTION && bind != STB_LOCAL) ||
        s.st_other > STV_PROTECTED ||
        (s.st_shndx >= SHN_LORESERVE && s.st_shndx != SHN_ABS))
      return DynamicError::Unsupported;
    if (!i) {
      if (s.st_name || s.st_info || s.st_other || s.st_shndx || s.st_value ||
          s.st_size)
        return DynamicError::Symbol;
    } else if (s.st_shndx == SHN_UNDEF) {
      if (bind == STB_LOCAL || s.st_value)
        return DynamicError::Symbol;
    } else if (s.st_shndx != SHN_ABS) {
      Address ignored;
      bool covered =
          image.runtime_address(s.st_value, s.st_size ? s.st_size : 1,
                                type == STT_FUNC ? PF_R | PF_X : PF_R, ignored);
      // Zero-sized data/boundary symbols may denote a LOAD's end, without
      // promising that one byte can be dereferenced at that address.
      if (!covered && !s.st_size && type != STT_FUNC) {
        for (unsigned j = 0; j < header->e_phnum; ++j) {
          Elf64_Phdr p;
          if (image.program_header(j, p) && p.p_type == PT_LOAD && p.p_memsz &&
              s.st_value >= p.p_vaddr && s.st_value - p.p_vaddr == p.p_memsz &&
              image.runtime_address(s.st_value, 0, PF_R, ignored))
            covered = true;
        }
      }
      if (!covered || (type == STT_FUNC && !is_aligned_entry(s.st_value)))
        return DynamicError::Symbol;
    }
  }
  bool any_rela = has(DynamicTag::Rela) || has(DynamicTag::Relasz) ||
                  has(DynamicTag::Relaent);
  if (any_rela) {
    Address size = val(DynamicTag::Relasz);
    if (!has(DynamicTag::Rela) || !has(DynamicTag::Relasz) ||
        !has(DynamicTag::Relaent) || val(DynamicTag::Relaent) != 24 ||
        size % 24)
      return DynamicError::Relocation;
    relas = image.file_backed_bytes(val(DynamicTag::Rela), size);
    if (relas.size() != size)
      return DynamicError::Relocation;
  }
  if ((!any_rela && has(DynamicTag::Relacount)) ||
      val(DynamicTag::Relacount) > relas.size() / 24)
    return DynamicError::Relocation;
  for (Address i = 0; i < relas.size() / 24; ++i) {
    DynamicRelocation r;
    Address ignored;
    if (!read_relocation(i, r) || !is_supported_dynamic_relocation(r.info))
      return DynamicError::Unsupported;
    if ((r.info >> 32) >= symbols.size() / 24 ||
        (i < val(DynamicTag::Relacount) && !is_relative_relocation(r.info)) ||
        !image.runtime_address(r.offset, 8, PF_R | PF_W, ignored))
      return DynamicError::Relocation;
    if (overlaps(r.offset, image.header_address(), 64) ||
        overlaps(r.offset, image.table_address(),
                 Address(header->e_phnum) * 56) ||
        overlaps(r.offset, dynamic_address, dynamic.size()) ||
        overlaps(r.offset, val(DynamicTag::Strtab), strings.size()) ||
        overlaps(r.offset, val(DynamicTag::Symtab), symbols.size()) ||
        overlaps(r.offset, val(DynamicTag::Hash), sysv.size()) ||
        overlaps(r.offset, val(DynamicTag::GnuHash), gnu.size()) ||
        overlaps(r.offset, val(DynamicTag::Rela), relas.size()))
      return DynamicError::Relocation;
    for (Address j = 0; j < i; ++j) {
      DynamicRelocation previous;
      if (!read_relocation(j, previous) ||
          overlaps(r.offset, previous.offset, 8))
        return DynamicError::Relocation;
    }
  }
  const DynamicTag addresses[] = {DynamicTag::InitArray, DynamicTag::FiniArray,
                                  DynamicTag::PreinitArray};
  for (unsigned i = 0; i < 3; ++i) {
    DynamicTag tag = addresses[i],
               size_tag = static_cast<DynamicTag>(slot(tag) + 1);
    Address size = val(size_tag);
    if (has(tag) != has(size_tag) || (has(tag) && i == 2 && !main) || size % 8)
      return DynamicError::Lifecycle;
    if (has(tag)) {
      arrays[i] = image.file_backed_bytes(val(tag), size);
      if (arrays[i].size() != size)
        return DynamicError::Lifecycle;
    }
  }
  const DynamicTag functions[] = {DynamicTag::Init, DynamicTag::Fini};
  for (DynamicTag t : functions) {
    Address ignored;
    if (has(t) && val(t) &&
        (!is_aligned_entry(val(t)) ||
         !image.runtime_address(val(t), 4, PF_R | PF_X, ignored)))
      return DynamicError::Lifecycle;
  }
  return DynamicError::None;
}

DynamicError DynamicInfo::open(const ElfImage &image, DynamicInfo &out) {
  out.clear();
  DynamicError error = out.parse(image);
  if (error == DynamicError::None)
    out.valid = true;
  else
    out.clear();
  return error;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
