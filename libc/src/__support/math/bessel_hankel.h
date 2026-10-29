//===-- Large-argument integer Bessel kernel --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_HANKEL_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_HANKEL_H

#include "src/__support/math/cos.h"
#include "src/__support/math/sin.h"
#include "src/__support/math/sqrt.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

struct HankelBesselPair {
  double j;
  double y;
};

// Internal RN kernel: n <= 2^31, finite x >= max(128, 16*n*n).
// DLMF 10.17.13-14 bounds the exact 32-term truncation independently of n;
// the first-neglected-term rule for small orders is not used at large n.
LIBC_INLINE HankelBesselPair bessel_hankel(unsigned n, double x) {
  double order = double(n);
  double mu = 4.0 * order * order;
  double p = 1.0, q = 0.0, term = 1.0;
  // Above this threshold corrections are negligible on the amplitude scale.
  // Avoid computing underflowing powers for very large x.
  double threshold = (mu > 1.0 ? mu : 1.0) * 0x1p70;
  if (x <= threshold) {
    for (unsigned k = 1; k < 32; ++k) {
      double odd = double(2 * k - 1);
      term *= ((mu - odd * odd) / (8.0 * double(k))) / x;
      double signed_term = (k & 2) ? -term : term;
      if (k & 1)
        q += signed_term;
      else
        p += signed_term;
      // All subsequent ratios through term 32 are below 1/8 in this domain.
      // The omitted finite tail and bounded asymptotic remainder are tiny.
      if (term > -0x1p-70 && term < 0x1p-70)
        break;
    }
  }

  // Reduce x itself, then rotate by the exact integer-order quadrant.
  // Subtracting a rounded n*pi/2 from x would destroy the phase for large x.
  double s = math::sin(x), c = math::cos(x);
  double cp = c + s, sp = s - c;
  switch (n & 3) {
  case 1: {
    double previous = cp;
    cp = sp;
    sp = -previous;
    break;
  }
  case 2:
    cp = -cp;
    sp = -sp;
    break;
  case 3: {
    double previous = cp;
    cp = -sp;
    sp = previous;
    break;
  }
  }
  constexpr double INV_SQRT_PI = 0.564189583547756286948;
  double amplitude = INV_SQRT_PI / math::sqrt(x);
  return {amplitude * (cp * p - sp * q), amplitude * (sp * p + cp * q)};
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_HANKEL_H
