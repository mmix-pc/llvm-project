//===-- Implementation of explicit_bzero ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/string/explicit_bzero.h"
#include "src/__support/common.h"
#include "src/string/memory_utils/inline_memset.h"

namespace LIBC_NAMESPACE_DECL {

[[gnu::noinline]] LLVM_LIBC_FUNCTION(void, explicit_bzero,
                                     (void *ptr, size_t count)) {
  inline_memset(ptr, 0, count);
  // Keep the stores even when the caller no longer observes the buffer.
  asm("" : : "r"(ptr) : "memory");
}

} // namespace LIBC_NAMESPACE_DECL
