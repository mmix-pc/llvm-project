//===-- Helpers for double hyperbolic functions -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_HYPERBOLIC_UTILS_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_HYPERBOLIC_UTILS_H

#include "exp.h"
#include "hdr/errno_macros.h"
#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/FPUtil/multiply_add.h"
#include "src/__support/FPUtil/rounding_mode.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {
namespace hyperbolic_internal {

// For x near log(2 * DBL_MAX), x - HI is exact and exp(x - HI) stays
// finite. Unlike squaring exp(x/2), this does not double its rounding error.
LIBC_INLINE double exp_half(double x, bool negative) {
  constexpr double HI =
      0x1.62e42fefa4000p-1; // Short upward approximation to log(2).
  // exp(HI - log(2)) - 1, rounded to nearest binary64.
  constexpr double CORRECTION = 0x1.8432a1b0e2881p-43;
  double r = math::exp(x - HI);
  if (negative)
    r = -r;
  return fputil::multiply_add(r, CORRECTION, r);
}

// For |x| < 2^-28, sinh(x) is just outside x and tanh(x) just inside x.
// Adjust bits instead of evaluating x^3, which would underflow prematurely.
LIBC_INLINE double tiny_odd(double x, bool outward) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  if (bits.is_zero())
    return x;
  uint64_t value = bits.uintval();
#ifndef LIBC_MATH_HAS_ASSUME_ROUND_NEAREST_ONLY
  int mode = fputil::quick_get_round();
  bool away = (mode == FE_UPWARD && !bits.is_neg()) ||
              (mode == FE_DOWNWARD && bits.is_neg());
  if (outward && away)
    ++value;
  if (!outward && mode != FE_TONEAREST && !away)
    --value;
#endif
  Bits result(value);
  fputil::raise_except_if_required(FE_INEXACT);
  if (result.abs().uintval() < Bits::min_normal().uintval()) {
    fputil::set_errno_if_required(ERANGE);
    fputil::raise_except_if_required(FE_UNDERFLOW);
  }
  return result.get_val();
}

LIBC_INLINE double overflow(bool negative) {
  using Bits = fputil::FPBits<double>;
  fputil::set_errno_if_required(ERANGE);
  fputil::raise_except_if_required(FE_OVERFLOW | FE_INEXACT);
  double result = Bits::inf().get_val();
#ifndef LIBC_MATH_HAS_ASSUME_ROUND_NEAREST_ONLY
  int mode = fputil::quick_get_round();
  if (mode == FE_TOWARDZERO || (mode == FE_DOWNWARD && !negative) ||
      (mode == FE_UPWARD && negative))
    result = Bits::max_normal().get_val();
#endif
  return negative ? -result : result;
}

} // namespace hyperbolic_internal
} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif
