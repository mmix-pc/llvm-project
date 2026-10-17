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
#include "src/__support/math/gamma_recurrence.h"
#include "src/__support/math/lgamma.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {
namespace gamma_double_internal {

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
