//===-- Wide gamma recurrence ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_GAMMA_RECURRENCE_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_GAMMA_RECURRENCE_H

#include "src/__support/FPUtil/NearestIntegerOperations.h"
#include "src/__support/math/tgamma_constants.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {
namespace gamma_double_internal {

// Evaluate gamma directly in a compact interval, then use
// Gamma(x+1)=x*Gamma(x). All argument shifts and products use integer-backed
// arithmetic: in particular neither rounding x+1 near zero nor sin(pi*x) near a
// pole loses input bits. The exponent is unbounded for this range, so an
// intermediate product cannot overflow or underflow even when the final gamma
// is subnormal.
LIBC_INLINE GammaWide gamma_recurrence(double x) {
  int shift = static_cast<int>(fputil::floor(x));
  if (x < static_cast<double>(shift) + 0.5)
    --shift;

  GammaWide input(x);
  GammaWide reduced =
      fputil::quick_sub(input, GammaWide(static_cast<double>(shift)));
  GammaWide z = fputil::quick_sub(reduced, GammaWide(1.0));
  GammaWide value = TGAMMA_COEFFS[192];
  for (int n = 191; n >= 0; --n)
    value = fputil::quick_add(fputil::quick_mul(value, z), TGAMMA_COEFFS[n]);

  GammaWide product(1.0);
  GammaWide factor = shift < 0 ? input : reduced;
  int count = shift < 0 ? -shift : shift;
  for (int n = 0; n < count; ++n) {
    product = fputil::quick_mul(product, factor);
    factor = fputil::quick_add(factor, GammaWide(1.0));
  }
  if (shift < 0) {
    // approx_reciprocal requires a positive operand.
    Sign sign = product.sign;
    product.sign = Sign::POS;
    product = fputil::approx_reciprocal(product);
    product.sign = sign;
  }
  return fputil::quick_mul(value, product);
}

} // namespace gamma_double_internal
} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_GAMMA_RECURRENCE_H
