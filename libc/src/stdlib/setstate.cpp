//===-- Implementation of setstate ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/setstate.h"
#include "src/__support/common.h"
#include "src/stdlib/random_utils.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, setstate, (char *buffer)) {
  return internal::random_set_state(buffer);
}
} // namespace LIBC_NAMESPACE_DECL
