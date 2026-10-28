//===-- CPU implementation of lgamma --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/lgamma.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/math/lgamma.h"
#ifdef LIBC_MATH_HAS_SIGNGAM
#include "src/math/signgam.h"
#endif

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(double, lgamma, (double x)) {
#ifdef LIBC_MATH_HAS_SIGNGAM
  return math::lgamma_r(x, &signgam);
#else
  return math::lgamma(x);
#endif
}

} // namespace LIBC_NAMESPACE_DECL
