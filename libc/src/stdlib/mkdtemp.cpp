//===-- Implementation of mkdtemp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/mkdtemp.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mkdir.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/null_check.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, mkdtemp, (char *pattern)) {
  LIBC_CRASH_ON_NULLPTR(pattern);
  cpp::string_view name(pattern);
  if (name.size() < 6 || name.substr(name.size() - 6) != "XXXXXX") {
    libc_errno = EINVAL;
    return nullptr;
  }
  char *suffix = pattern + name.size() - 6;
  constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  for (unsigned attempt = 0; attempt < 256; ++attempt) {
    unsigned char bytes[6];
    size_t filled = 0;
    while (filled != sizeof(bytes)) {
      auto result =
          linux_syscalls::getrandom(bytes + filled, sizeof(bytes) - filled, 0);
      if (!result.has_value()) {
        if (result.error() == EINTR)
          continue;
        libc_errno = result.error();
        return nullptr;
      }
      if (result.value() == 0) {
        libc_errno = EIO;
        return nullptr;
      }
      filled += static_cast<size_t>(result.value());
    }
    for (unsigned i = 0; i < sizeof(bytes); ++i)
      suffix[i] = alphabet[bytes[i] & 63];
    auto result = linux_syscalls::mkdir(pattern, 0700);
    if (result.has_value())
      return pattern;
    if (result.error() != EEXIST) {
      libc_errno = result.error();
      return nullptr;
    }
  }
  libc_errno = EEXIST;
  return nullptr;
}

} // namespace LIBC_NAMESPACE_DECL
