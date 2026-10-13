//===-- Final loader image protections ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_PROTECTION_H
#define LLVM_LIBC_LOADER_LINUX_PROTECTION_H

#include "loader/linux/elf.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Call once after relocation, before publication or any user execution.
// Borrow immutable, nonaliasing mapped images; ownership remains with callers.
// Preflight every layout before any syscall. Failures may leave permissions
// partially tightened: abort startup and release owned mappings, never continue
// or restore writable permissions to retry. Returns negative Linux errno.
long finalize_image_protections(cpp::span<const ElfImage *const> images,
                                Address page_size);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_PROTECTION_H
