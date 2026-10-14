//===-- Startup TLS layout planning ------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_TLS_LAYOUT_H
#define LLVM_LIBC_LOADER_LINUX_TLS_LAYOUT_H

#include "loader/linux/elf.h"
#include "src/__support/OSUtil/linux/loader_services.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

enum class TLSLayoutError { None, Scope, Storage, Overflow };

// Internal incremental primitive shared by planning and relocation preflight.
// The image is admitted and immutable; ID/generation are assigned separately.
bool place_tls_module(const ElfImage &image, Address &cursor,
                      StartupTLSModule &module);

// Input is the immutable, deduplicated main-first BFS startup scope.
// Descriptors borrow runtime template addresses; file views may be used for
// planning tests, never as proof that those addresses can be read in the
// current process. Storage must not alias input or out and must outlive the
// returned set. No mapping, template copying, relocation, publication or
// late-IE reserve is performed. Failure clears out and leaves descriptor
// storage untouched.
TLSLayoutError plan_startup_tls(cpp::span<const ElfImage *const> images,
                                cpp::span<StartupTLSModule> storage,
                                StartupTLSSet &out);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
