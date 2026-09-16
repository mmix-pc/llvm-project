//===-- MMIX Linux prepared restoration --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_RESTORE_H
#define LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_RESTORE_H

#include "hdr/types/jmp_buf.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
[[noreturn]] void restore_jump(const __jmp_buf *env, int value, bool restore_mask);
}

#endif
