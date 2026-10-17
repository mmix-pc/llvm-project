//===-- Binary64 implementation of cpowl --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/complex/cpowl.h"
#include "src/__support/macros/properties/types.h"

#if defined(LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64)
#include "src/__support/common.h"
#include "src/complex/cpow.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(_Complex long double, cpowl,
                   (_Complex long double x, _Complex long double y)) {
  return static_cast<_Complex long double>(LIBC_NAMESPACE::cpow(
      static_cast<_Complex double>(x), static_cast<_Complex double>(y)));
}

} // namespace LIBC_NAMESPACE_DECL
#endif // LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64
