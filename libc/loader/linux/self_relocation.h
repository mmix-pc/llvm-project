//===-- Loader self relocation -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_SELF_RELOCATION_H
#define LLVM_LIBC_LOADER_LINUX_SELF_RELOCATION_H

#include "loader/linux/elf.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

struct WritableRegion {
  Address vaddr;
  cpp::span<unsigned char> bytes;
};

enum class RelocationError {
  None,
  Image,
  Dynamic,
  Unsupported,
  Table,
  Destination,
  Overlap,
  Value
};

// Startup-only, with no concurrent mutation. Preflight every record before
// writing any byte. Writable views must refer to the admitted image's storage;
// relocation destinations cannot overlap headers, dynamic or relocation tables.
RelocationError relocate_self(const ElfImage &image,
                              cpp::span<const WritableRegion> writable);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_SELF_RELOCATION_H
