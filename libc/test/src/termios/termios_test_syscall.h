//===-- Test boundary for termios syscalls -----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_TERMIOS_TERMIOS_TEST_SYSCALL_H
#define LLVM_LIBC_TEST_SRC_TERMIOS_TERMIOS_TEST_SYSCALL_H

#include "src/__support/OSUtil/linux/syscall.h"

namespace LIBC_NAMESPACE_DECL {
ErrorOr<int> termios_test_syscall(long, int, unsigned long, unsigned long);

namespace linux_syscalls {
template <>
LIBC_INLINE ErrorOr<int>
syscall_checked<int, int, unsigned long, unsigned long>(long number, int fd,
                                                        unsigned long request,
                                                        unsigned long arg) {
  return termios_test_syscall(number, fd, request, arg);
}
} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

#endif
