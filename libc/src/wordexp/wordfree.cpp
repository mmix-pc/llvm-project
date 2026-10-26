//===-- Implementation of wordfree ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/wordfree.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/wordexp/results.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, wordfree, (wordexp_t *result)) {
  int saved_errno = libc_errno;
  internal::release_word_results(*result);
  libc_errno = saved_errno;
}
} // namespace LIBC_NAMESPACE_DECL
