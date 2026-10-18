//===-- ErrorOr-returning Linux mknodat wrapper -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SYSCALL_WRAPPERS_MKNODAT_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SYSCALL_WRAPPERS_MKNODAT_H

#include "hdr/errno_macros.h"
#include "hdr/types/dev_t.h"
#include "hdr/types/mode_t.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {

LIBC_INLINE ErrorOr<unsigned int> encode_device(dev_t dev) {
  // Linux transports device numbers in a 32-bit syscall argument.
  if (static_cast<unsigned int>(dev) != dev)
    return Error(EINVAL);
  return static_cast<unsigned int>(dev);
}

#ifdef SYS_mknodat
LIBC_INLINE ErrorOr<int> mknodat(int dfd, const char *path, mode_t mode,
                                 dev_t dev) {
  auto encoded = encode_device(dev);
  if (!encoded)
    return Error(encoded.error());
  int ret = syscall_impl<int>(SYS_mknodat, dfd, path, mode, *encoded);
  if (ret < 0)
    return Error(-ret);
  return ret;
}
#endif

} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

#endif
