//===-- Loader entry state ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_ENTRY_H
#define LLVM_LIBC_LOADER_LINUX_ENTRY_H

#include "loader/linux/elf.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

enum class AuxField : unsigned {
  Phdr,
  Phent,
  Phnum,
  Pagesz,
  Base,
  Entry,
  Random,
  Secure,
  Uid,
  Euid,
  Gid,
  Egid,
  Count
};

enum class EntryError {
  None,
  Stack,
  Arguments,
  Environment,
  Auxv,
  Duplicate,
  Secure,
  Missing,
  AddressRange,
  ProgramHeaders,
  Image,
  Entry,
  Random
};

struct InitialStack {
  Address original_sp, argc, argv, envp, auxv;
  Address values[static_cast<unsigned>(AuxField::Count)];
  bool big_endian;
  bool valid = false;
  Address value(AuxField field) const {
    return values[static_cast<unsigned>(field)];
  }
};

struct MainLocation {
  Address bias, header_vaddr, table_vaddr, header_runtime;
  bool valid = false;
};

// The caller supplies a readable initial-stack extent, including any auxv
// padding. Only pointer values are inspected: no argv/env string is read here.
// In particular, AT_SECURE is rejected before any environment interpretation.
EntryError decode_initial_stack(Bytes bytes, Address sp, bool big_endian,
                                InitialStack &out);

// Preliminary discovery from the independently readable AT_PHDR table. The
// result locates the main header; it is not full image admission by itself.
EntryError locate_main_image(const InitialStack &stack, Bytes phdrs,
                             MainLocation &out);

// Images must already be admitted mapped views. random describes a separately
// readable runtime region, not a linked-address image region. self_pc is the
// actual interpreter entry PC captured by its assembly entry.
EntryError validate_entry_images(const InitialStack &stack,
                                 const MainLocation &main_location,
                                 const ElfImage &self, const ElfImage &main,
                                 Address self_pc, const MappedRegion &random);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_ENTRY_H
