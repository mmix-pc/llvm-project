//===-- Linux loader target admission ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_TARGET_H
#define LLVM_LIBC_LOADER_LINUX_TARGET_H

#include "hdr/elf_proxy.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// The caller supplies a decoded header. File bounds, magic, versions and image
// role are the common parser's responsibility, not this machine identity hook.
bool is_supported_target(const Elf64_Ehdr &header);
bool is_supported_page_size(Elf64_Xword page_size);
bool is_user_range(Elf64_Addr address, Elf64_Xword size);
bool is_aligned_entry(Elf64_Addr address);
const char *interpreter_path();
bool is_relative_relocation(Elf64_Xword info);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_LOADER_LINUX_TARGET_H
