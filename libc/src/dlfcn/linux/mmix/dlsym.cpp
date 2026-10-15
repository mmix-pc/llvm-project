//===-- MMIX runtime dlsym ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dlfcn/dlsym.h"
#include "loading.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void *, dlsym,
                   (void *__restrict handle, const char *__restrict name)) {
  return mmix::loader_lookup(
      handle, name, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
}
} // namespace LIBC_NAMESPACE_DECL
