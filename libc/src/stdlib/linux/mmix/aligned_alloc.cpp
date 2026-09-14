//===-- MMIX Linux aligned_alloc -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/aligned_alloc.h"
#include "allocator.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void *, aligned_alloc, (size_t alignment, size_t size)) {
  if (!alignment || (alignment & (alignment - 1)) || size % alignment) {
    libc_errno = EINVAL;
    return nullptr;
  }
  void *ptr = mmix::allocate(size, alignment);
  if (!ptr && size)
    libc_errno = ENOMEM;
  return ptr;
}

} // namespace LIBC_NAMESPACE_DECL
