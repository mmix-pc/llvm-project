//===-- MMIX loader raw syscall adapter -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/syscall.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

long raw_syscall(long number, long a1, long a2, long a3, long a4, long a5,
                 long a6) {
  return __llvm_libc_mmix_syscall(static_cast<unsigned long>(number), a1, a2,
                                  a3, a4, a5, a6);
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
