//===-- Implementation of mktemp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/mktemp.h"
#include "hdr/errno_macros.h"
#include "hdr/types/struct_stat.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/null_check.h"
#include "src/sys/stat/lstat.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, mktemp, (char *pattern)) {
  LIBC_CRASH_ON_NULLPTR(pattern);
  int saved_errno = libc_errno;
  cpp::string_view name(pattern);
  if (name.size() < 6 || name.substr(name.size() - 6) != "XXXXXX") {
    libc_errno = EINVAL;
    pattern[0] = '\0';
    return pattern;
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
        pattern[0] = '\0';
        return pattern;
      }
      if (result.value() == 0) {
        libc_errno = EIO;
        pattern[0] = '\0';
        return pattern;
      }
      filled += static_cast<size_t>(result.value());
    }
    for (unsigned i = 0; i < sizeof(bytes); ++i)
      suffix[i] = alphabet[bytes[i] & 63];
    struct stat status;
    if (LIBC_NAMESPACE::lstat(pattern, &status) == -1) {
      if (libc_errno == ENOENT) {
        libc_errno = saved_errno;
        return pattern;
      }
      pattern[0] = '\0';
      return pattern;
    }
  }
  libc_errno = EEXIST;
  pattern[0] = '\0';
  return pattern;
}

} // namespace LIBC_NAMESPACE_DECL
