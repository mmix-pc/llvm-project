//===-- Implementation header for erfc --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Adapted from fdlibm s_erf.c:
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_ERFC_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_ERFC_H

#include "exp.h"
#include "hdr/errno_macros.h"
#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/FPUtil/dyadic_float.h"
#include "src/__support/FPUtil/except_value_utils.h"
#include "src/__support/FPUtil/rounding_mode.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

namespace erfc_internal {
using Wide = fputil::DyadicFloat<128>;
using fputil::quick_add;
using fputil::quick_mul;
using fputil::quick_sub;

LIBC_INLINE Wide polyeval(const Wide &, double a) { return Wide(a); }

template <typename... Rest>
LIBC_INLINE Wide polyeval(const Wide &x, double a, Rest... rest) {
  return quick_add(Wide(a), quick_mul(x, polyeval(x, rest...)));
}

// All denominators used here are positive. Keep the quotient in software
// precision so directed hardware rounding does not accumulate in Horner steps.
LIBC_INLINE Wide divide(const Wide &a, const Wide &b) {
  return quick_mul(a, fputil::approx_reciprocal(b));
}

LIBC_INLINE Wide exp_small(const Wide &x) {
  // |x| < 1/2 throughout the tail reduction. The degree-20 Taylor remainder
  // is at most exp(1/2) * (1/2)^21 / 21! < 2^-85. Evaluate the factorial
  // recurrence in software precision, without rounded binary64 coefficients.
  Wide result(1.0);
  for (int k = 20; k > 0; --k)
    result =
        quick_add(Wide(1.0), divide(quick_mul(x, result), Wide(double(k))));
  return result;
}

LIBC_INLINE double finish(const Wide &value) {
  double result = value.as<double, false>();
  fputil::raise_except_if_required(FE_INEXACT);
  if (fputil::FPBits<double>(result).uintval() <
      fputil::FPBits<double>::min_normal().uintval()) {
    fputil::set_errno_if_required(ERANGE);
    fputil::raise_except_if_required(FE_UNDERFLOW);
  }
  return result;
}

// The exact positive result is smaller than half the least subnormal.
template <typename T> LIBC_INLINE T underflow() {
  fputil::set_errno_if_required(ERANGE);
  fputil::raise_except_if_required(FE_UNDERFLOW | FE_INEXACT);
#ifndef LIBC_MATH_HAS_ASSUME_ROUND_NEAREST_ONLY
  if (fputil::quick_get_round() == FE_UPWARD)
    return fputil::FPBits<T>::min_subnormal().get_val();
#endif
  return T(0);
}
} // namespace erfc_internal

LIBC_INLINE double erfc(double x) {
  using Bits = fputil::FPBits<double>;
  using namespace erfc_internal;
  Bits bits(x);
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  if (bits.is_inf())
    return bits.is_neg() ? 2.0 : 0.0;
  if (bits.is_zero())
    return 1.0;

  double ax = bits.abs().get_val();
  if (ax < 0.84375) {
    // Avoid squaring tiny inputs, whose result is normal and near one.
    if (ax < 0x1p-56)
      return bits.is_neg() ? fputil::round_result_slightly_up(1.0)
                           : fputil::round_result_slightly_down(1.0);
    Wide wx(x);
    Wide z = quick_mul(wx, wx);
    Wide p = polyeval(z, 1.28379167095512558561e-01,
                      -3.25042107247001499370e-01, -2.84817495755985104766e-02,
                      -5.77027029648944159157e-03, -2.37630166566501626084e-05);
    Wide q = polyeval(z, 1.0, 3.97917223959155352819e-01,
                      6.50222499887672944485e-02, 5.08130628187576562776e-03,
                      1.32494738004321644526e-04, -3.96022827877536812320e-06);
    Wide y = divide(p, q);
    return finish(quick_sub(quick_sub(Wide(1.0), wx), quick_mul(wx, y)));
  }
  if (ax < 1.25) {
    Wide s(ax - 1.0);
    Wide p = polyeval(s, -2.36211856075265944077e-03,
                      4.14856118683748331666e-01, -3.72207876035701323847e-01,
                      3.18346619901161753674e-01, -1.10894694282396677476e-01,
                      3.54783043256182359371e-02, -2.16637559486879084300e-03);
    Wide q =
        polyeval(s, 1.0, 1.06420880400844228286e-01, 5.40397917702171048937e-01,
                 7.18286544141962662868e-02, 1.26171219808761642112e-01,
                 1.36370839120290507362e-02, 1.19844998467991074170e-02);
    constexpr double ERX = 8.45062911510467529297e-01;
    Wide y = divide(p, q);
    return finish(bits.is_neg() ? quick_add(Wide(1.0 + ERX), y)
                                : quick_sub(Wide(1.0 - ERX), y));
  }
  if (bits.is_neg() && ax >= 6.0)
    return fputil::round_result_slightly_down(2.0);
  if (ax >= 28.0)
    return erfc_internal::underflow<double>();

  // Approximate log(erfc(x) * x) + x^2 + 9/16, not 1 - erf(x).
  Wide wax(ax);
  Wide s = fputil::approx_reciprocal(quick_mul(wax, wax));
  Wide p, q;
  if (ax < 0x1.6db6dp+1) {
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
  // Truncate to 21 significant bits so the leading square is exact.
  double z =
      Bits(uint64_t(bits.abs().uintval() & 0xffff'ffff'0000'0000ULL)).get_val();
  Wide correction =
      quick_add(quick_mul(Wide(z - ax), quick_add(Wide(z), wax)), divide(p, q));
  Wide result;
  if (ax < 26.0) {
    result = divide(
        quick_mul(Wide(math::exp(-z * z - 0.5625)), exp_small(correction)),
        wax);
  } else {
    // Split 512 * log(2); the leading exponent sum is exact. Keep exp and
    // division normal, with underflow possible only in the final scaling.
    constexpr double HI = 0x1.62e42fee00000p+8;
    constexpr double LO = 0x1.a39ef35793c76p-24;
    result = divide(quick_mul(Wide(math::exp((-z * z - 0.5625) + HI)),
                              exp_small(quick_add(correction, Wide(LO)))),
                    wax);
    result.exponent -= 512;
  }
  return finish(bits.is_neg() ? quick_sub(Wide(2.0), result) : result);
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_ERFC_H
