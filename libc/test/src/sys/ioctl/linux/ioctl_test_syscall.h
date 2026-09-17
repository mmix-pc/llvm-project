//===-- Test boundary for Linux ioctl syscalls -------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_SYS_IOCTL_LINUX_IOCTL_TEST_SYSCALL_H
#define LLVM_LIBC_TEST_SRC_SYS_IOCTL_LINUX_IOCTL_TEST_SYSCALL_H

#include "src/__support/OSUtil/linux/syscall.h"

namespace LIBC_NAMESPACE_DECL {

ErrorOr<int> ioctl_test_syscall(long number, int fd, unsigned long request,
                                unsigned long arg);

// Retain the real public varargs entrypoint and errno conversion. Do not issue
// terminal or privileged device operations on the host running these tests.
namespace linux_syscalls {
template <>
LIBC_INLINE ErrorOr<int>
syscall_checked<int, int, unsigned long, unsigned long>(long number, int fd,
                                                        unsigned long request,
                                                        unsigned long arg) {
  return ioctl_test_syscall(number, fd, request, arg);
}
} // namespace linux_syscalls

} // namespace LIBC_NAMESPACE_DECL

#endif
