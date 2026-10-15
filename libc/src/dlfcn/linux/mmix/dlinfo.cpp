//===-- MMIX resident image query -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dlfcn/dlinfo.h"
#include "loading.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, dlinfo, (void *handle, int request, void *out)) {
  return mmix::loader_info(handle, request, out);
}
} // namespace LIBC_NAMESPACE_DECL
