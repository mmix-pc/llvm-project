//===-- Linux implementation of posix_fallocate ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/fcntl/posix_fallocate.h"

#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

// Select only for a reviewed native LP64 syscall ABI (MMIX and x86-64).
// ILP32 needs architecture-specific splitting and alignment for loff_t.
static_assert(sizeof(long) == 8 && sizeof(void *) == 8 && sizeof(off_t) == 8 &&
                  cpp::numeric_limits<off_t>::is_signed,
              "posix_fallocate requires a native LP64 syscall ABI");

LLVM_LIBC_FUNCTION(int, posix_fallocate, (int fd, off_t offset, off_t len)) {
  if (offset < 0 || len <= 0)
    return EINVAL;
  // Match Linux vfs_fallocate without evaluating an overflowing signed sum.
  if (offset > cpp::numeric_limits<off_t>::max() - len)
    return EFBIG;

  // Mode zero reserves space and may extend the file. Unsupported filesystems
  // return their error; neither zero writes nor ftruncate can replace this.
  long ret =
      LIBC_NAMESPACE::syscall_impl<long>(SYS_fallocate, fd, 0, offset, len);
  return ret < 0 ? static_cast<int>(-ret) : 0;
}

} // namespace LIBC_NAMESPACE_DECL
