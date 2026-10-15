//===-- Resident runtime unwind lookup ---------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_UNWIND_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_UNWIND_H

#include "loader/linux/runtime_scopes.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
// Context is a process-resident RuntimeScopes. Lookup/release acquire its
// immutable snapshot, without allocation, locks or application callbacks.
long find_runtime_unwind_image(void *, uintptr_t, UnwindImageView *);
void release_runtime_unwind_image(void *, uintptr_t);
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
