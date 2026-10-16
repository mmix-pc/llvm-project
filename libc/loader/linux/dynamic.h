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
  Lifecycle
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
  Count
};

struct DynamicRelocation {
  Address offset, info, addend; // addend retains the signed ELF bit pattern.
};

class DynamicInfo {
  Address values[static_cast<unsigned>(DynamicTag::Count)];
  Address seen = 0, needed_size = 0;
  Bytes dynamic, strings, symbols, relas, sysv, gnu, arrays[3];
  bool big = true, valid = false;
  bool tls_required = false;

  void clear();
  DynamicError parse(const ElfImage &image);
  DynamicError validate_hashes(const ElfImage &image);
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
  // First externally visible definition in the validated hash chain.
  bool find_definition(Bytes name, Address &index) const;
  Bytes sysv_hash() const { return valid ? sysv : Bytes(); }
  Bytes gnu_hash() const { return valid ? gnu : Bytes(); }
  Bytes lifecycle_array(DynamicTag tag) const;
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_DYNAMIC_H
