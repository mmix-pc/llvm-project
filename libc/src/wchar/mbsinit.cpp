//===-- Implementation of mbsinit -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/mbsinit.h"

#include "hdr/types/mbstate_t.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/wchar/mbstate.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mbsinit, (const mbstate_t *ps)) {
  if (ps == nullptr)
    return true;
  auto state = *reinterpret_cast<const internal::mbstate *>(ps);
  // All supported encodings use zero named fields for the initial state.
  // Cached designations and pending UTF-16 units are therefore non-initial.
  return state.partial == 0 && state.bytes_stored == 0 && state.total_bytes == 0;
}

} // namespace LIBC_NAMESPACE_DECL
