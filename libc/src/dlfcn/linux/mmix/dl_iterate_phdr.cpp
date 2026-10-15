//===-- MMIX resident image query -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/link/dl_iterate_phdr.h"
#include "loading.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, dl_iterate_phdr,
                   (__dl_iterate_phdr_callback_t callback, void *argument)) {
  if (!callback)
    return mmix::loader_iterate(nullptr, argument);
  struct Callback {
    __dl_iterate_phdr_callback_t function;
    void *argument;
  } state{callback, argument};
  return mmix::loader_iterate(
      [](void *info, void *opaque) {
        auto &state = *static_cast<Callback *>(opaque);
        return state.function(static_cast<dl_phdr_info *>(info),
                              sizeof(dl_phdr_info), state.argument);
      },
      &state);
}
} // namespace LIBC_NAMESPACE_DECL
