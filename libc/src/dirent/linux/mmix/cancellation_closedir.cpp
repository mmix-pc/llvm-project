//===-- MMIX Linux cancellation-safe closedir ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/closedir.h"
#include "src/__support/File/dir.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation.h"
#include <dirent.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, closedir, (::DIR *directory)) {
  mmix::test_cancel();
  auto *dir = reinterpret_cast<Dir *>(directory);
  // Linux consumes the fd even on close errors. No cancellation point follows
  // consumption; successful Dir::close has already released the object.
  int result = dir->close(/*consume_on_error=*/true);
  if (result) {
    libc_errno = result;
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
