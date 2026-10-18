//===-- Implementation of c32rtomb----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/uchar/c32rtomb.h"
#include "src/uchar/conversion.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(size_t, c32rtomb,
                   (char *__restrict s, char32_t c, mbstate_t *__restrict ps)) {
  static internal::mbstate implicit_state;
  auto *state =
      ps ? reinterpret_cast<internal::mbstate *>(ps) : &implicit_state;
  return internal::encode(s, c, state);
}
} // namespace LIBC_NAMESPACE_DECL
