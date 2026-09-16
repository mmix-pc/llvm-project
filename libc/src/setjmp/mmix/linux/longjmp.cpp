//===-- Linux longjmp for MMIX --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "restore.h"
#include "src/__support/common.h"
#include "src/setjmp/longjmp.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, longjmp, (jmp_buf env, int value)) {
  restore_jump(env, value, false);
}
} // namespace LIBC_NAMESPACE_DECL
