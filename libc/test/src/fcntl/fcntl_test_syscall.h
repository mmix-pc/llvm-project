//===-- Test boundary for Linux fcntl syscalls ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_FCNTL_FCNTL_TEST_SYSCALL_H
#define LLVM_LIBC_TEST_SRC_FCNTL_FCNTL_TEST_SYSCALL_H

#include "hdr/fcntl_macros.h"
#include "hdr/types/struct_f_owner_ex.h"
#include "hdr/types/struct_flock64.h"
#include "src/__support/OSUtil/linux/syscall.h"

namespace LIBC_NAMESPACE_DECL {

int fcntl_test_syscall(long number, int fd, int cmd, void *arg);

// Specialize only the raw syscall instantiations used by the real fcntl
// wrapper. The production entrypoint and command/error translation stay intact.
template <>
LIBC_INLINE int syscall_impl<int, int, int, void *>(long number, int fd,
                                                    int cmd, void *arg) {
  return fcntl_test_syscall(number, fd, cmd, arg);
}

template <>
LIBC_INLINE int
syscall_impl<int, int, int, struct flock64 *>(long number, int fd, int cmd,
                                              struct flock64 *arg) {
  return fcntl_test_syscall(number, fd, cmd, arg);
}

template <>
LIBC_INLINE int
syscall_impl<int, int, int, struct f_owner_ex *>(long number, int fd, int cmd,
                                                 struct f_owner_ex *arg) {
  return fcntl_test_syscall(number, fd, cmd, arg);
}

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_TEST_SRC_FCNTL_FCNTL_TEST_SYSCALL_H
