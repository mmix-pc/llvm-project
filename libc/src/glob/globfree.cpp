//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/glob/globfree.h"
#include "hdr/func/free.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, globfree, (glob_t * result)) {
  int saved = libc_errno;
  if (result->gl_pathv) {
    for (size_t i = 0; i < result->gl_pathc; ++i)
      ::free(result->gl_pathv[result->gl_offs + i]);
    ::free(result->gl_pathv);
  }
  result->gl_pathv = nullptr;
  result->gl_pathc = 0;
  libc_errno = saved;
}
} // namespace LIBC_NAMESPACE_DECL
