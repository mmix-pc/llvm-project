//===-- MMIX Linux loader target admission --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

bool is_supported_target(const Elf64_Ehdr &header) {
  return header.e_machine == EM_MMIX &&
         header.e_ident[EI_CLASS] == ELFCLASS64 &&
         header.e_ident[EI_DATA] == ELFDATA2MSB &&
         (header.e_ident[EI_OSABI] == ELFOSABI_NONE ||
          header.e_ident[EI_OSABI] == ELFOSABI_LINUX) &&
         header.e_ident[EI_ABIVERSION] == 0 && header.e_flags == 0;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
