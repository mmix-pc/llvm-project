// Integer-order recurrences adapted from NetBSD e_jn.c at
// 5b0e0d5997518246c017c6577adcb59be12d0a79.
//
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_RECURRENCE_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_RECURRENCE_H

#include "src/__support/FPUtil/dyadic_float.h"
#include "src/__support/math/bessel0.h"
#include "src/__support/math/bessel1.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

using BesselFloat = fputil::DyadicFloat<128>;

struct ScaledBesselPair {
  BesselFloat j;
  BesselFloat y;
};

// Internal RN kernel: 2 <= n <= 2048, finite x > 0. Keep an independent
// exponent until public range handling. Even at the smallest x, the internal
// exponents stay within a signed 32-bit integer for this bounded order range.
LIBC_INLINE ScaledBesselPair bessel_recurrence(unsigned n, double x) {
  using fputil::approx_reciprocal;
  using fputil::quick_mul;
  using fputil::quick_sub;
  BesselFloat reciprocal = approx_reciprocal(BesselFloat(x));
  BesselFloat two_over_x = reciprocal;
  ++two_over_x.exponent;
  double j0 = bessel0_internal::j0(x);
  // The backward normalization selects j0 for tiny x. Avoid evaluating a
  // rounded subnormal j1 that is not used there.
  double j1 = x < 0x1p-54 ? 0.0 : bessel1_internal::j1(x);
  BesselFloat a(j0), b(j1);
  BesselFloat j;
  if (x >= double(n)) {
    for (unsigned k = 1; k < n; ++k) {
      BesselFloat next = quick_sub(
          quick_mul(quick_mul(BesselFloat(double(k)), two_over_x), b), a);
      a = b;
      b = next;
    }
    j = b;
  } else {
    // Every remaining continued-fraction tail is in [0,1]. Propagating this
    // bracket through 128 levels gives width < 2^-89 at the worst case
    // n=x=2048; smaller x or order contracts at least as fast.
    BesselFloat t(0.0);
    for (unsigned k = n + 128; k >= n; --k)
      t = approx_reciprocal(
          quick_sub(quick_mul(BesselFloat(double(k)), two_over_x), t));
    a = t;
    b = BesselFloat(1.0);
    for (unsigned k = n - 1; k > 0; --k) {
      BesselFloat next = quick_sub(
          quick_mul(quick_mul(BesselFloat(double(k)), two_over_x), b), a);
      a = b;
      b = next;
    }
    double abs0 = j0 < 0 ? -j0 : j0, abs1 = j1 < 0 ? -j1 : j1;
    BesselFloat denominator = abs0 >= abs1 ? b : a;
    BesselFloat numerator(abs0 >= abs1 ? j0 : j1);
    // approx_reciprocal expects a positive operand.
    if (denominator.sign.is_neg()) {
      denominator.sign = Sign::POS;
      numerator = -numerator;
    }
    j = quick_mul(quick_mul(t, numerator), approx_reciprocal(denominator));
  }

  a = BesselFloat(bessel0_internal::y0(x));
  // y1(x) ~ -2/(pi*x) here; retain its exponent instead of overflowing a
  // binary64 intermediate. The omitted relative correction is below 2^-100.
  b = x < 0x1p-54 ? quick_mul(BesselFloat(-bessel1_internal::tpi), reciprocal)
                  : BesselFloat(bessel1_internal::y1(x));
  for (unsigned k = 1; k < n; ++k) {
    BesselFloat next = quick_sub(
        quick_mul(quick_mul(BesselFloat(double(k)), two_over_x), b), a);
    a = b;
    b = next;
  }
  return {j, b};
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_RECURRENCE_H
