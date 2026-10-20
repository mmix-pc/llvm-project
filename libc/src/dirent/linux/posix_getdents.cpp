//===-- Linux implementation of posix_getdents
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/posix_getdents.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/File/dir.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <stddef.h>
#ifdef LIBC_FULL_BUILD
#include <dirent.h>
// Direct kernel transport is valid only for the Linux getdents64 wire layout.
static_assert(sizeof(ino_t) == 8);
static_assert(offsetof(struct posix_dent, d_reclen) == 16);
static_assert(offsetof(struct posix_dent, d_type) == 18);
static_assert(offsetof(struct posix_dent, d_name) == 19);
static_assert(offsetof(struct posix_dent, d_ino) ==
              offsetof(struct dirent, d_ino));
static_assert(offsetof(struct posix_dent, d_reclen) ==
              offsetof(struct dirent, d_reclen));
static_assert(offsetof(struct posix_dent, d_type) ==
              offsetof(struct dirent, d_type));
static_assert(offsetof(struct posix_dent, d_name) ==
              offsetof(struct dirent, d_name));
#endif

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(ssize_t, posix_getdents,
                   (int fd, void *buffer, size_t size, int flags)) {
  if (flags != 0) {
    libc_errno = EOPNOTSUPP;
    return -1;
  }
  // Linux accepts an unsigned int count, but limits the result to INT_MAX.
  size_t limit = static_cast<size_t>(cpp::numeric_limits<int>::max());
  if (size > limit)
    size = limit;
  auto result = platform_fetch_dirents(
      fd, cpp::span<uint8_t>(static_cast<uint8_t *>(buffer), size));
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return static_cast<ssize_t>(result.value());
}
} // namespace LIBC_NAMESPACE_DECL
