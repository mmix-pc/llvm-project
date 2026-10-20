//===-- Implementation of readdir_r ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/readdir_r.h"
#include "src/__support/File/dir.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, readdir_r,
                   (DIR *__restrict dir, struct dirent *__restrict entry,
                    struct dirent **__restrict result)) {
  int saved_errno = libc_errno;
  auto next = reinterpret_cast<Dir *>(dir)->read_copy(entry);
  libc_errno = saved_errno;
  *result = next ? next.value() : nullptr;
  return next ? 0 : next.error();
}
} // namespace LIBC_NAMESPACE_DECL
