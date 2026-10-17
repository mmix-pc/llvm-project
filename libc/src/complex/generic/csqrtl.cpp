//===-- Binary64 implementation of csqrtl -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/complex/csqrtl.h"
#include "src/__support/macros/properties/types.h"

#if defined(LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64)
#include "src/__support/common.h"
#include "src/complex/csqrt.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(_Complex long double, csqrtl, (_Complex long double x)) {
  return static_cast<_Complex long double>(
      LIBC_NAMESPACE::csqrt(static_cast<_Complex double>(x)));
}

} // namespace LIBC_NAMESPACE_DECL
#endif // LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64
