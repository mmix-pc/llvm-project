//===-- Implementation of rand_r ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/rand_r.h"
#include "hdr/stdint_proxy.h"
#include "hdr/stdlib_macros.h"
#include "src/__support/common.h"
#include "src/__support/macros/null_check.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, rand_r, (unsigned int *seed)) {
  LIBC_CRASH_ON_NULLPTR(seed);
  // Xorshift32 keeps all state in the caller's object. Zero is a fixed point,
  // so use the same nonzero initialization convention as srand.
  uint32_t state = *seed ? static_cast<uint32_t>(*seed) : 1;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  *seed = state;
  return static_cast<int>(state & RAND_MAX);
}

} // namespace LIBC_NAMESPACE_DECL
