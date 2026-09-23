//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Adapted from fdlibm:
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_ERF_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_ERF_H

#include "exp.h"
#include "hdr/errno_macros.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/FPUtil/PolyEval.h"
#include "src/__support/FPUtil/except_value_utils.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE double erf(double x) {
  using Bits = fputil::FPBits<double>;
  using fputil::polyeval;
  Bits bits(x);
  if (bits.is_nan())
    return x + x;
  if (bits.is_inf())
    return bits.is_neg() ? -1.0 : 1.0;
  if (bits.is_zero())
    return x;

  double ax = bits.abs().get_val();
  if (ax < 0.84375) {
    // erf(x) = x + x * P(x^2) / Q(x^2). Scale subnormals to avoid
    // underflow in the correction term before rounding the final result.
    if (ax < 0x1p-28) {
      double result = ax < 0x1p-1015
                          ? 0.125 * (8.0 * x + 1.02703333676410069053 * x)
                          : x + 0.128379167095512586316 * x;
      if (Bits(result).abs().uintval() < Bits::min_normal().uintval()) {
        fputil::set_errno_if_required(ERANGE);
        fputil::raise_except_if_required(FE_UNDERFLOW | FE_INEXACT);
      }
      return result;
    }
    double z = x * x;
    double p =
        polyeval(z, 1.28379167095512558561e-01, -3.25042107247001499370e-01,
                 -2.84817495755985104766e-02, -5.77027029648944159157e-03,
                 -2.37630166566501626084e-05);
    double q =
        polyeval(z, 1.0, 3.97917223959155352819e-01, 6.50222499887672944485e-02,
                 5.08130628187576562776e-03, 1.32494738004321644526e-04,
                 -3.96022827877536812320e-06);
    return x + x * (p / q);
  }
  if (ax < 1.25) {
    // Approximate erf near one after subtracting a 24-bit anchor.
    double s = ax - 1.0;
    double p =
        polyeval(s, -2.36211856075265944077e-03, 4.14856118683748331666e-01,
                 -3.72207876035701323847e-01, 3.18346619901161753674e-01,
                 -1.10894694282396677476e-01, 3.54783043256182359371e-02,
                 -2.16637559486879084300e-03);
    double q =
        polyeval(s, 1.0, 1.06420880400844228286e-01, 5.40397917702171048937e-01,
                 7.18286544141962662868e-02, 1.26171219808761642112e-01,
                 1.36370839120290507362e-02, 1.19844998467991074170e-02);
    constexpr double ERX = 8.45062911510467529297e-01;
    return bits.is_neg() ? -ERX - p / q : ERX + p / q;
  }
  if (ax >= 6.0)
    return bits.is_neg() ? fputil::round_result_slightly_up(-1.0)
                         : fputil::round_result_slightly_down(1.0);

  // The tail is exp(-x^2 - 9/16 + P(1/x^2)/Q(1/x^2)) / x.
  double s = 1.0 / (ax * ax);
  double p, q;
  if (ax < 0x1.6db6ep+1) {
    p = polyeval(s, -9.86494403484714822705e-03, -6.93858572707181764372e-01,
                 -1.05586262253232909814e+01, -6.23753324503260060396e+01,
                 -1.62396669462573470355e+02, -1.84605092906711035994e+02,
                 -8.12874355063065934246e+01, -9.81432934416914548592e+00);
    q = polyeval(s, 1.0, 1.96512716674392571292e+01, 1.37657754143519042600e+02,
                 4.34565877475229228821e+02, 6.45387271733267880336e+02,
                 4.29008140027567833386e+02, 1.08635005541779435134e+02,
                 6.57024977031928170135e+00, -6.04244152148580987438e-02);
  } else {
    p = polyeval(s, -9.86494292470009928597e-03, -7.99283237680523006574e-01,
                 -1.77579549177547519889e+01, -1.60636384855821916062e+02,
                 -6.37566443368389627722e+02, -1.02509513161107724954e+03,
                 -4.83519191608651397019e+02);
    q = polyeval(s, 1.0, 3.03380607434824582924e+01, 3.25792512996573918826e+02,
                 1.53672958608443695994e+03, 3.19985821950859553908e+03,
                 2.55305040643316442583e+03, 4.74528541206955367215e+02,
                 -2.24409524465858183362e+01);
  }
  // Splitting x makes the leading square exact.
  double z =
      Bits(uint64_t(bits.abs().uintval() & 0xffff'ffff'0000'0000ULL)).get_val();
  double r =
      math::exp(-z * z - 0.5625) * math::exp((z - ax) * (z + ax) + p / q);
  return bits.is_neg() ? r / ax - 1.0 : 1.0 - r / ax;
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif
