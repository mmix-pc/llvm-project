//===-- Implementation header for vfork --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UNISTD_VFORK_H
#define LLVM_LIBC_SRC_UNISTD_VFORK_H

#include "hdr/types/pid_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
[[gnu::returns_twice]] pid_t vfork()
#if defined(__mmix__) && defined(__linux__)
    // Both returns must enter the caller without an intervening C++ frame.
    __asm__("__llvm_libc_mmix_vfork")
#endif
    ;
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_UNISTD_VFORK_H
