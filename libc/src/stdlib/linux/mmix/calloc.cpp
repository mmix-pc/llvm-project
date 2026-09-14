//===-- MMIX Linux calloc
//--------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/calloc.h"
#include "allocator.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/string/memory_utils/inline_memset.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void *, calloc, (size_t count, size_t size)) {
  size_t bytes;
  if (__builtin_mul_overflow(count, size, &bytes)) {
    libc_errno = ENOMEM;
    return nullptr;
  }
  void *ptr = mmix::allocate(bytes);
  if (ptr)
    inline_memset(ptr, 0, bytes);
  else if (bytes)
    libc_errno = ENOMEM;
  return ptr;
}

} // namespace LIBC_NAMESPACE_DECL
