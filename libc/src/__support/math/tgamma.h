//===-- CPU gamma implementation -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMA_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMA_H

#include "src/__support/FPUtil/dyadic_float.h"
#include "src/__support/math/lgamma.h"
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

template <typename T> LIBC_INLINE T gamma(T x) {
  using Bits = fputil::FPBits<T>;
  using Wide = fputil::DyadicFloat<128>;
  Bits bits(x);
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  if (bits.is_inf() && !bits.is_neg())
    return x;
  if (bits.is_zero()) {
    fputil::set_errno_if_required(ERANGE);
    fputil::raise_except_if_required(FE_DIVBYZERO);
    return Bits::inf(bits.sign()).get_val();
  }
  if (bits.is_neg() && (bits.is_inf() || gamma_internal::is_integer(x))) {
    fputil::set_errno_if_required(EDOM);
    fputil::raise_except_if_required(FE_INVALID);
    return Bits::quiet_nan().get_val();
  }

  double xd = static_cast<double>(x);
  double a = bits.abs().get_val();
  if (a < 0x1p-70) {
    // Laurent expansion. Evaluate the reciprocal without a floating-point
    // overflow, retaining guard bits for the subtraction of Euler's constant.
    Wide reciprocal = fputil::approx_reciprocal(Wide(a));
    reciprocal.sign = bits.sign();
    Wide correction = TGAMMA_COEFFS[1];
    // For extremely tiny x, retain a negative sticky tail even when Euler's
    // constant is beyond the 128-bit accumulator. A binary64 reciprocal that
    // is not exact is separated from a rounding boundary by > 2^-108 relative;
    // this 2^-120 tail only decides directed rounding of an exact reciprocal.
    int e = reciprocal.get_unbiased_exponent();
    if (e > 120) {
      correction = Wide(-1.0);
      correction.exponent += e - 120;
    }
    Wide result = fputil::quick_add(reciprocal, correction);
    fputil::raise_except_if_required(FE_INEXACT);
    return result.template generic_as<T, true>();
  }

  if (xd > 172.0)
    return overflow<T>();

  // Up to 35!, the odd part of the factorial fits in 128 bits. Accumulate it
  // exactly, then round once (including float overflow), without exp/log noise.
  if (xd > 0.0 && xd <= 36.0 && gamma_internal::is_integer(x)) {
    Wide result(1.0);
    for (int n = 2; n < static_cast<int>(xd); ++n)
      result = fputil::quick_mul(result, Wide(static_cast<double>(n)));
    return result.template as<T, true>();
  }

  int sign = 1;
  if (xd < -200.0) {
    // Nonintegers are necessarily smaller than 2^52 in magnitude. Their
    // distance from a pole is >= 2^-45 here; reflection and Stirling bound
    // |Gamma(x)| well below half the least binary64 subnormal.
    double n = fputil::floor(-xd);
    sign = (static_cast<uint64_t>(n) & 1) ? 1 : -1;
    return underflow<T>(sign < 0 ? Sign::NEG : Sign::POS);
  }

  Wide result = gamma_recurrence(xd);
  // Keep the sign through the final conversion so directed rounding also
  // works for negative subnormals. Full-range correct rounding is not proven.
  return result.template generic_as<T, true>();
}

} // namespace gamma_double_internal

LIBC_INLINE double tgamma(double x) { return gamma_double_internal::gamma(x); }

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMA_H
