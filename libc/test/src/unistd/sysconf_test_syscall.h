//===-- Test boundary for sysconf queries -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_UNISTD_SYSCONF_TEST_SYSCALL_H
#define LLVM_LIBC_TEST_SRC_UNISTD_SYSCONF_TEST_SYSCALL_H

#include "hdr/types/pid_t.h"
#include "hdr/types/struct_rlimit.h"
#include "hdr/types/struct_sysinfo.h"
#include "src/__support/OSUtil/linux/syscall.h"

namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {

ErrorOr<int> sysconf_test_prlimit(long number, pid_t pid, int resource,
                                  const struct rlimit *new_limit,
                                  struct rlimit *old_limit);
ErrorOr<int> sysconf_test_sysinfo(long number, struct sysinfo *info);

// Inject query results while retaining the real sysconf entrypoint and its
// ErrorOr-to-errno translation.
template <>
LIBC_INLINE ErrorOr<int>
syscall_checked<int, pid_t, int, const struct rlimit *, struct rlimit *>(
    long number, pid_t pid, int resource, const struct rlimit *new_limit,
    struct rlimit *old_limit) {
  return sysconf_test_prlimit(number, pid, resource, new_limit, old_limit);
}

template <>
LIBC_INLINE ErrorOr<int>
syscall_checked<int, struct sysinfo *>(long number, struct sysinfo *info) {
  return sysconf_test_sysinfo(number, info);
}

} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

#endif
