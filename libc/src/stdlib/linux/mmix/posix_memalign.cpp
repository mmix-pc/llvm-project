//===-- MMIX Linux posix_memalign ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "posix_memalign.h"
#include "allocator.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_memalign,
                   (void **result, size_t alignment, size_t size)) {
  if (!alignment || (alignment & (alignment - 1)) ||
      alignment % sizeof(void *))
    return EINVAL;
  int saved_errno = libc_errno;
  void *ptr = mmix::allocate(size, alignment);
  libc_errno = saved_errno;
  if (!ptr && size)
    return ENOMEM;
  *result = ptr;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
