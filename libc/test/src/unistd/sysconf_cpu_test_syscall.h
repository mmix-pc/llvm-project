//===-- Test boundary for sysconf CPU queries -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_UNISTD_SYSCONF_CPU_TEST_SYSCALL_H
#define LLVM_LIBC_TEST_SRC_UNISTD_SYSCONF_CPU_TEST_SYSCALL_H

#include "hdr/fcntl_macros.h"
#include "hdr/types/mode_t.h"
#include "hdr/types/pid_t.h"
#include "hdr/types/ssize_t.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
int sysconf_cpu_test_open(long number, int dirfd, const char *path, int flags,
                          mode_t mode);
ssize_t sysconf_cpu_test_read(long number, int fd, void *buffer, size_t size);
int sysconf_cpu_test_close(long number, int fd);
int sysconf_cpu_test_affinity(long number, pid_t tid, size_t size,
                              unsigned char *mask);

#ifdef SYS_openat
template <>
LIBC_INLINE int syscall_impl<int, int, const char *, int, mode_t>(
    long number, int dirfd, const char *path, int flags, mode_t mode) {
  return sysconf_cpu_test_open(number, dirfd, path, flags, mode);
}
#else
template <>
LIBC_INLINE int syscall_impl<int, const char *, int, mode_t>(
    long number, const char *path, int flags, mode_t mode) {
  return sysconf_cpu_test_open(number, AT_FDCWD, path, flags, mode);
}
#endif

template <>
LIBC_INLINE ssize_t syscall_impl<ssize_t, int, void *, size_t>(
    long number, int fd, void *buffer, size_t size) {
  return sysconf_cpu_test_read(number, fd, buffer, size);
}

template <>
LIBC_INLINE int syscall_impl<int, int>(long number, int fd) {
  return sysconf_cpu_test_close(number, fd);
}

template <>
LIBC_INLINE int syscall_impl<int, pid_t, size_t, unsigned char *>(
    long number, pid_t tid, size_t size, unsigned char *mask) {
  return sysconf_cpu_test_affinity(number, tid, size, mask);
}
} // namespace LIBC_NAMESPACE_DECL

#endif
