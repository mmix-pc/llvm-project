//===-- Implementation of mbrtoc32----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/uchar/mbrtoc32.h"
#include "src/uchar/conversion.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(size_t, mbrtoc32,
                   (char32_t *__restrict out, const char *__restrict s,
                    size_t n, mbstate_t *__restrict ps)) {
  static LIBC_THREAD_LOCAL internal::mbstate implicit_state;
  auto *state =
      ps ? reinterpret_cast<internal::mbstate *>(ps) : &implicit_state;
  return internal::decode(out, s, n, state);
}
} // namespace LIBC_NAMESPACE_DECL
