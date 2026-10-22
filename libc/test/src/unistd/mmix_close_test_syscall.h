//===-- Test boundary for MMIX close completion -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_UNISTD_MMIX_CLOSE_TEST_SYSCALL_H
#define LLVM_LIBC_TEST_SRC_UNISTD_MMIX_CLOSE_TEST_SYSCALL_H

#include "src/__support/OSUtil/linux/syscall.h"

namespace LIBC_NAMESPACE_DECL {
int mmix_close_test_syscall(long number, int fd);
template <>
LIBC_INLINE int syscall_impl<int, int>(long number, int fd) {
  return mmix_close_test_syscall(number, fd);
}
} // namespace LIBC_NAMESPACE_DECL

#endif
