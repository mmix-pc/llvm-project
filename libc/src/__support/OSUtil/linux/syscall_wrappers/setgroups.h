//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SYSCALL_WRAPPERS_SETGROUPS_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SYSCALL_WRAPPERS_SETGROUPS_H

#include "hdr/types/gid_t.h"
#include "hdr/types/size_t.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/error_or.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {
LIBC_INLINE ErrorOr<int> setgroups(size_t size, const gid_t *list) {
#if defined(SYS_setgroups32)
  constexpr long number = SYS_setgroups32;
#else
  constexpr long number = SYS_setgroups;
#endif
  return syscall_checked<int>(number, size, list);
}
} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

#endif
