//===-- Implementation of erand48 ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/erand48.h"
#include "src/__support/common.h"
#include "src/stdlib/random48_utils.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(double, erand48, (unsigned short state[3])) {
  return static_cast<double>(internal::random48_next(state)) * 0x1p-48;
}
} // namespace LIBC_NAMESPACE_DECL
