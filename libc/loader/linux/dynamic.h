//===-- Checked dynamic metadata ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_DYNAMIC_H
#define LLVM_LIBC_LOADER_LINUX_DYNAMIC_H

#include "loader/linux/elf.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

enum class DynamicError {
  None,
  Image,
  Table,
  Unsupported,
  Duplicate,
  String,
  Symbol,
  Hash,
  Relocation,
  Lifecycle,
  Version
};

// NEEDED is repeated and ordered; all entries represented here are singletons.
enum class DynamicTag {
  Strtab,
  Strsz,
  Symtab,
  Syment,
  Hash,
  GnuHash,
  Soname,
  Rpath,
  Runpath,
  Rela,
  Relasz,
  Relaent,
  Relacount,
  Init,
  Fini,
  InitArray,
  InitArraySize,
  FiniArray,
  FiniArraySize,
  PreinitArray,
  PreinitArraySize,
  Flags,
  Flags1,
  BindNow,
  Debug,
  Checksum,
  Versym,
  Verdef,
  Verdefnum,
  Verneed,
  Verneednum,
  Count
};
static_assert(static_cast<unsigned>(DynamicTag::Count) <= 64);

struct DynamicRelocation {
  Address offset, info, addend; // addend retains the signed ELF bit pattern.
};

class DynamicInfo {
  Address values[static_cast<unsigned>(DynamicTag::Count)];
  Address seen = 0, needed_size = 0;
  Bytes dynamic, strings, symbols, relas, sysv, gnu, arrays[3];
  Bytes versym, verdef, verneed;
  bool big = true, valid = false;
  bool tls_required = false;
  const ElfImage *source = nullptr;

  void clear();
  DynamicError parse(const ElfImage &image);
  DynamicError validate_hashes(const ElfImage &image);
  DynamicError validate_versions(const ElfImage &image);
  bool version_name(Address index, bool definition, Bytes &out) const;
  bool find_definition_impl(Bytes name, Address &index, Bytes version) const;
  bool read_string(Address offset, Bytes &out) const;
  bool read_symbol(Address index, Elf64_Sym &out) const;
  bool read_relocation(Address index, DynamicRelocation &out) const;

public:
  DynamicInfo();
  // Borrows immutable metadata, with no allocation or section-header access.
  // Failed opens invalidate every accessor. Lifecycle arrays remain unexecuted
  // and their pointer contents are not interpreted until after relocation.
  static DynamicError open(const ElfImage &image, DynamicInfo &out);
  bool is_valid() const { return valid; }
  // Reuse only for the original immutable image view, not another mapping of
  // the same file. Copying this metadata does not extend the backing lifetime.
  bool is_for(const ElfImage &image) const { return valid && source == &image; }
  bool has(DynamicTag tag) const {
    return valid && (seen & (Address(1) << static_cast<unsigned>(tag)));
  }
  bool requires_tls() const { return valid && tls_required; }
  Address value(DynamicTag tag) const {
    return has(tag) ? values[static_cast<unsigned>(tag)] : 0;
  }
  Address symbol_count() const { return valid ? symbols.size() / 24 : 0; }
  Address relocation_count() const { return valid ? relas.size() / 24 : 0; }
  Address needed_count() const { return valid ? needed_size : 0; }
  bool string(Address offset, Bytes &out) const {
    return valid && read_string(offset, out);
  }
  bool symbol(Address index, Elf64_Sym &out) const {
    return valid && read_symbol(index, out);
  }
  bool relocation(Address index, DynamicRelocation &out) const {
    return valid && read_relocation(index, out);
  }
  bool needed(Address index, Bytes &out) const;
  // Empty version selects only public defaults. Nonempty versions match
  // exactly; an unversioned definition is never a compatibility fallback.
  bool find_definition(Bytes name, Address &index, Bytes version = {}) const;
  bool symbol_version(Address index, Bytes &name) const;
  bool symbol_is_local(Address index) const;
  // Check the actual dependency selected for DT_NEEDED, before relocation.
  bool accepts_dependency(Bytes needed, const DynamicInfo &provider) const;
  bool defines_version(Bytes name) const;
  Bytes sysv_hash() const { return valid ? sysv : Bytes(); }
  Bytes gnu_hash() const { return valid ? gnu : Bytes(); }
  Bytes lifecycle_array(DynamicTag tag) const;
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_DYNAMIC_H
