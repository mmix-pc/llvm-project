//===-- Stable Bessel transition variables -----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_TRANSITION_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_TRANSITION_H

#include "src/__support/math/cbrt.h"
#include "src/__support/math/sqrt.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

struct BesselTransition {
  double zeta;
  double amplitude;
  double phase;
};

// Positive integral n <= 2^31, |1-(x/n)^2| <= 1/4, round-to-nearest.
// DLMF 10.20.2--5: zeta changes sign at x=n; phase is 2*|zeta|^(3/2)/3.
// Keeping x-n avoids losing an input displacement when x/n rounds to one.
LIBC_INLINE BesselTransition bessel_transition(double n, double x) {
  double delta = (x - n) / n;
  double signed_square = -delta * (2.0 + delta);
  double square = signed_square < 0 ? -signed_square : signed_square;

  // atanh(w)-w and w-atan(w), divided by w^3. The former has positive
  // terms, the latter alternating terms. With w^2 <= 1/4, the omitted
  // tail after 32 terms is <= (1/4)^32 / (67*(1-1/4)) < 2^-69.
  double series = 1.0 / 65.0;
  for (int k = 30; k >= 0; --k)
    series = 1.0 / (2.0 * k + 3.0) + signed_square * series;
  double root = math::cbrt(1.5 * series);
  double ratio = root * root;
  return {signed_square * ratio, math::sqrt(math::sqrt(4.0 * ratio)),
          square * math::sqrt(square) * series};
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_TRANSITION_H
