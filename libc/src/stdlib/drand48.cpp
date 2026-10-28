//===-- Implementation of drand48 ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/drand48.h"
#include "src/__support/common.h"
#include "src/stdlib/random48_utils.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(double, drand48, ()) {
  return static_cast<double>(internal::random48_next()) * 0x1p-48;
}
} // namespace LIBC_NAMESPACE_DECL
