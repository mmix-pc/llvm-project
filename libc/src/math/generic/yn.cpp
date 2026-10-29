//===-- Implementation of yn ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/yn.h"
#include "src/__support/common.h"
#include "src/__support/math/bessel_integer.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(double, yn, (int n, double x)) {
  return math::integer_bessel<true>(n, x);
}
} // namespace LIBC_NAMESPACE_DECL
