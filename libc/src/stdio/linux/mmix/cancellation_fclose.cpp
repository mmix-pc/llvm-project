//===-- MMIX Linux cancellation-safe fclose -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/fclose.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, fclose, (::FILE *stream)) {
  mmix::test_cancel();
  // Flush may unwind while the object is still published. Removal, buffer
  // release and descriptor consumption then use only noncanceling raw paths.
  int result = reinterpret_cast<File *>(stream)->close();
  if (result) {
    libc_errno = result;
    return EOF;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
