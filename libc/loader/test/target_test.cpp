//===-- Loader target identity tests --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/target.h"

using LIBC_NAMESPACE::loader::is_supported_target;

extern "C" int main() {
  Elf64_Ehdr header = {};
  header.e_machine = EM_MMIX;
  header.e_ident[EI_CLASS] = ELFCLASS64;
  header.e_ident[EI_DATA] = ELFDATA2MSB;
  if (!is_supported_target(header))
    return 1;
  header.e_ident[EI_OSABI] = ELFOSABI_LINUX;
  if (!is_supported_target(header))
    return 2;

  auto invalid = header;
  invalid.e_machine = EM_X86_64;
  if (is_supported_target(invalid))
    return 3;
  invalid = header;
  invalid.e_ident[EI_CLASS] = ELFCLASS32;
  if (is_supported_target(invalid))
    return 4;
  invalid = header;
  invalid.e_ident[EI_DATA] = ELFDATA2LSB;
  if (is_supported_target(invalid))
    return 5;
  invalid = header;
  invalid.e_ident[EI_OSABI] = 255;
  if (is_supported_target(invalid))
    return 6;
  invalid = header;
  invalid.e_ident[EI_ABIVERSION] = 1;
  if (is_supported_target(invalid))
    return 7;
  invalid = header;
  invalid.e_flags = 1;
  if (is_supported_target(invalid))
    return 8;
  return 0;
}
