//===-- Wide Bessel phase and uniform variables -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_PHASE_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_PHASE_H

#include "src/__support/math/atan_utils.h"
#include "src/__support/math/cbrt.h"
#include "src/__support/math/sqrt.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {
namespace bessel_phase_internal {

using Wide = fputil::DyadicFloat<128>;
using fputil::quick_add;
using fputil::quick_mul;
using fputil::quick_sub;
using LIBC_NAMESPACE::operator""_u128;

LIBC_INLINE Wide divide(const Wide &a, const Wide &positive_b) {
  return quick_mul(a, fputil::approx_reciprocal(positive_b));
}

// Only called for positive normal binary64-sized squared magnitudes.
LIBC_INLINE Wide square_root(const Wide &a) {
  Wide root(math::sqrt(double(a)));
  for (unsigned i = 0; i < 2; ++i) {
    root = quick_add(root, divide(a, root));
    --root.exponent;
  }
  return root;
}

// Positive q <= 1, with the existing atan table's bounded residual.
LIBC_INLINE Wide arctangent(const Wide &q) {
  unsigned i = static_cast<unsigned>(double(q) * 64.0 + 0.5);
  Wide grid(double(i) / 64.0);
  Wide residual =
      divide(quick_sub(q, grid), quick_add(Wide(1.0), quick_mul(q, grid)));
  return quick_add(atan_internal::ATAN_I_F128[i],
                   atan_internal::atan_eval(residual));
}

struct LogCoefficients {
  Wide values[48]{};
  constexpr LogCoefficients() {
    for (unsigned k = 0; k < 48; ++k)
      values[k] = fputil::approx_reciprocal(Wide(double(2 * k + 1)));
  }
};
inline constexpr LogCoefficients LOG_COEFFICIENTS;

// log(a) for a >= 1. Normalize first, so the atanh argument is at most 1/3.
// The exact 48-term tail is below 2^-156, independently of a's exponent.
LIBC_INLINE Wide logarithm(Wide a) {
  constexpr Wide LN2(Sign::POS, -128,
                     0xb17217f7'd1cf79ab'c9e3b398'03f2f6af_u128);
  int exponent = a.get_unbiased_exponent();
  a.exponent -= exponent;
  Wide q = divide(quick_sub(a, Wide(1.0)), quick_add(a, Wide(1.0)));
  Wide square = quick_mul(q, q);
  Wide sum = LOG_COEFFICIENTS.values[47];
  for (int k = 46; k >= 0; --k)
    sum = quick_add(LOG_COEFFICIENTS.values[k], quick_mul(square, sum));
  sum = quick_mul(q, sum);
  ++sum.exponent;
  return quick_add(quick_mul(Wide(double(exponent)), LN2), sum);
}

} // namespace bessel_phase_internal

struct BesselPhase {
  bessel_phase_internal::Wide phase;
  double square;
  double zeta;
  double amplitude;
};

// Internal RN geometry: integral 2048 <= n <= 2^31,
// finite 0 < x <= 16*n*n. phase = 2*n*|zeta|^(3/2)/3.
LIBC_INLINE BesselPhase bessel_phase(double n, double x) {
  using namespace bessel_phase_internal;
  if (x == n)
    return {Wide(0.0), 0.0, 0.0, 1.259921049894873164767};
  Wide order(n), argument(x);
  Wide difference =
      quick_sub(quick_mul(order, order), quick_mul(argument, argument));
  bool oscillatory = x > n;
  if (oscillatory)
    difference = -difference;
  Wide root = square_root(difference);
  Wide phase;
  if (oscillatory) {
    Wide angle;
    if (double(root) <= n) {
      angle = arctangent(divide(root, order));
    } else {
      Wide half_pi = atan_internal::ATAN_I_F128[64];
      ++half_pi.exponent;
      angle = quick_sub(half_pi, arctangent(divide(order, root)));
    }
    phase = quick_sub(root, quick_mul(order, angle));
  } else {
    Wide ratio = divide(quick_add(order, root), argument);
    phase = quick_sub(quick_mul(order, logarithm(ratio)), root);
  }
  double square = double(divide(difference, quick_mul(order, order)));
  double c = math::cbrt(1.5 * double(divide(phase, order)));
  double zeta = c * c;
  double amplitude = math::sqrt(math::sqrt(4.0 * zeta / square));
  return {phase, oscillatory ? -square : square, oscillatory ? -zeta : zeta,
          amplitude};
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_PHASE_H
