//===-- MMIX Linux realloc
//-------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/realloc.h"
#include "allocator.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void *, realloc, (void *ptr, size_t size)) {
  int saved_errno = libc_errno;
  void *result = mmix::resize(ptr, size);
  libc_errno = !result && size ? ENOMEM : saved_errno;
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
