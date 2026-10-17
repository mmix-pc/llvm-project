//===-- Binary64 log-gamma implementation ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The approximation below is adapted from newlib/libm/math/er_lgamma.c.
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_LGAMMA_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_LGAMMA_H

#include "hdr/errno_macros.h"
#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/FPUtil/NearestIntegerOperations.h"
#include "src/__support/FPUtil/rounding_mode.h"
#include "src/__support/macros/config.h"
#include "src/__support/math/cos.h"
#include "src/__support/math/gamma_recurrence.h"
#include "src/__support/math/gamma_util.h"
#include "src/__support/math/log.h"
#include "src/__support/math/sin.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {
namespace gamma_double_internal {

// Avoid cancellation in the reflection formula near zeros of log|Gamma(x)|.
// Retain the recurrence's guard bits through log rather than first rounding
// Gamma(x) to binary64. Normalize to [3/4, 3/2), where |z| <= 1/5 in
// log(m) = 2 * atanh(z), z = (m - 1) / (m + 1).
LIBC_INLINE double log_gamma_recurrence(double x, int &sign) {
  GammaWide value = gamma_recurrence(x);
  sign = value.sign == Sign::NEG ? -1 : 1;
  value.sign = Sign::POS;
  int exponent = value.get_unbiased_exponent();
  value.exponent -= exponent;
  if (value.mantissa >= GammaWide(1.5).mantissa) {
    --value.exponent;
    ++exponent;
  }
  GammaWide one(1.0);
  GammaWide z = fputil::quick_mul(
      fputil::quick_sub(value, one),
      fputil::approx_reciprocal(fputil::quick_add(value, one)));
  GammaWide z2 = fputil::quick_mul(z, z);
  GammaWide term = z;
  GammaWide sum = z;
  // The omitted tail is below 2^-185; arithmetic is evaluated at 128 bits.
  for (int n = 1; n < 40; ++n) {
    term = fputil::quick_mul(term, z2);
    GammaWide reciprocal =
        fputil::approx_reciprocal(GammaWide(static_cast<double>(2 * n + 1)));
    sum = fputil::quick_add(sum, fputil::quick_mul(term, reciprocal));
  }
  ++sum.exponent;
  sum = fputil::quick_add(
      sum, fputil::quick_mul(GammaWide(static_cast<double>(exponent)),
                            log_internal::LOG_2));
  return sum.template generic_as<double, true>();
}

// fdlibm's three primary-interval approximations, recurrence to [2,3],
// and Stirling correction. This is not a correctly-rounded algorithm.
LIBC_INLINE double positive_log_gamma(double x) {
  constexpr double zero = 0.0;
  constexpr double half =
                       5.00000000000000000000e-01, /* 0x3FE00000, 0x00000000 */
      one = 1.00000000000000000000e+00,            /* 0x3FF00000, 0x00000000 */
      a0 = 7.72156649015328655494e-02,             /* 0x3FB3C467, 0xE37DB0C8 */
      a1 = 3.22467033424113591611e-01,             /* 0x3FD4A34C, 0xC4A60FAD */
      a2 = 6.73523010531292681824e-02,             /* 0x3FB13E00, 0x1A5562A7 */
      a3 = 2.05808084325167332806e-02,             /* 0x3F951322, 0xAC92547B */
      a4 = 7.38555086081402883957e-03,             /* 0x3F7E404F, 0xB68FEFE8 */
      a5 = 2.89051383673415629091e-03,             /* 0x3F67ADD8, 0xCCB7926B */
      a6 = 1.19270763183362067845e-03,             /* 0x3F538A94, 0x116F3F5D */
      a7 = 5.10069792153511336608e-04,             /* 0x3F40B6C6, 0x89B99C00 */
      a8 = 2.20862790713908385557e-04,             /* 0x3F2CF2EC, 0xED10E54D */
      a9 = 1.08011567247583939954e-04,             /* 0x3F1C5088, 0x987DFB07 */
      a10 = 2.52144565451257326939e-05,            /* 0x3EFA7074, 0x428CFA52 */
      a11 = 4.48640949618915160150e-05,            /* 0x3F07858E, 0x90A45837 */
      tc = 1.46163214496836224576e+00,             /* 0x3FF762D8, 0x6356BE3F */
      tf = -1.21486290535849611461e-01,            /* 0xBFBF19B9, 0xBCC38A42 */
      /* tt = -(tail of tf) */
      tt = -3.63867699703950536541e-18,  /* 0xBC50C7CA, 0xA48A971F */
      t0 = 4.83836122723810047042e-01,   /* 0x3FDEF72B, 0xC8EE38A2 */
      t1 = -1.47587722994593911752e-01,  /* 0xBFC2E427, 0x8DC6C509 */
      t2 = 6.46249402391333854778e-02,   /* 0x3FB08B42, 0x94D5419B */
      t3 = -3.27885410759859649565e-02,  /* 0xBFA0C9A8, 0xDF35B713 */
      t4 = 1.79706750811820387126e-02,   /* 0x3F9266E7, 0x970AF9EC */
      t5 = -1.03142241298341437450e-02,  /* 0xBF851F9F, 0xBA91EC6A */
      t6 = 6.10053870246291332635e-03,   /* 0x3F78FCE0, 0xE370E344 */
      t7 = -3.68452016781138256760e-03,  /* 0xBF6E2EFF, 0xB3E914D7 */
      t8 = 2.25964780900612472250e-03,   /* 0x3F6282D3, 0x2E15C915 */
      t9 = -1.40346469989232843813e-03,  /* 0xBF56FE8E, 0xBF2D1AF1 */
      t10 = 8.81081882437654011382e-04,  /* 0x3F4CDF0C, 0xEF61A8E9 */
      t11 = -5.38595305356740546715e-04, /* 0xBF41A610, 0x9C73E0EC */
      t12 = 3.15632070903625950361e-04,  /* 0x3F34AF6D, 0x6C0EBBF7 */
      t13 = -3.12754168375120860518e-04, /* 0xBF347F24, 0xECC38C38 */
      t14 = 3.35529192635519073543e-04,  /* 0x3F35FD3E, 0xE8C2D3F4 */
      u0 = -7.72156649015328655494e-02,  /* 0xBFB3C467, 0xE37DB0C8 */
      u1 = 6.32827064025093366517e-01,   /* 0x3FE4401E, 0x8B005DFF */
      u2 = 1.45492250137234768737e+00,   /* 0x3FF7475C, 0xD119BD6F */
      u3 = 9.77717527963372745603e-01,   /* 0x3FEF4976, 0x44EA8450 */
      u4 = 2.28963728064692451092e-01,   /* 0x3FCD4EAE, 0xF6010924 */
      u5 = 1.33810918536787660377e-02,   /* 0x3F8B678B, 0xBF2BAB09 */
      v1 = 2.45597793713041134822e+00,   /* 0x4003A5D7, 0xC2BD619C */
      v2 = 2.12848976379893395361e+00,   /* 0x40010725, 0xA42B18F5 */
      v3 = 7.69285150456672783825e-01,   /* 0x3FE89DFB, 0xE45050AF */
      v4 = 1.04222645593369134254e-01,   /* 0x3FBAAE55, 0xD6537C88 */
      v5 = 3.21709242282423911810e-03,   /* 0x3F6A5ABB, 0x57D0CF61 */
      s0 = -7.72156649015328655494e-02,  /* 0xBFB3C467, 0xE37DB0C8 */
      s1 = 2.14982415960608852501e-01,   /* 0x3FCB848B, 0x36E20878 */
      s2 = 3.25778796408930981787e-01,   /* 0x3FD4D98F, 0x4F139F59 */
      s3 = 1.46350472652464452805e-01,   /* 0x3FC2BB9C, 0xBEE5F2F7 */
      s4 = 2.66422703033638609560e-02,   /* 0x3F9B481C, 0x7E939961 */
      s5 = 1.84028451407337715652e-03,   /* 0x3F5E26B6, 0x7368F239 */
      s6 = 3.19475326584100867617e-05,   /* 0x3F00BFEC, 0xDD17E945 */
      r1 = 1.39200533467621045958e+00,   /* 0x3FF645A7, 0x62C4AB74 */
      r2 = 7.21935547567138069525e-01,   /* 0x3FE71A18, 0x93D3DCDC */
      r3 = 1.71933865632803078993e-01,   /* 0x3FC601ED, 0xCCFBDF27 */
      r4 = 1.86459191715652901344e-02,   /* 0x3F9317EA, 0x742ED475 */
      r5 = 7.77942496381893596434e-04,   /* 0x3F497DDA, 0xCA41A95B */
      r6 = 7.32668430744625636189e-06,   /* 0x3EDEBAF7, 0xA5B38140 */
      w0 = 4.18938533204672725052e-01,   /* 0x3FDACFE3, 0x90C97D69 */
      w1 = 8.33333333333329678849e-02,   /* 0x3FB55555, 0x5555553B */
      w2 = -2.77777777728775536470e-03,  /* 0xBF66C16C, 0x16B02E5C */
      w3 = 7.93650558643019558500e-04,   /* 0x3F4A019F, 0x98CF38B6 */
      w4 = -5.95187557450339963135e-04,  /* 0xBF4380CB, 0x8C0FE741 */
      w5 = 8.36339918996282139126e-04,   /* 0x3F4B67BA, 0x4CDAD5D1 */
      w6 = -1.63092934096575273989e-03;  /* 0xBF5AB89D, 0x0B9E43E4 */
  uint64_t bits = fputil::FPBits<double>(x).uintval();
  uint32_t ix = static_cast<uint32_t>(bits >> 32);
  uint32_t lx = static_cast<uint32_t>(bits);
  int i;
  double t, y, z, p, p1, p2, p3, q, r, w;
  /* purge off 1 and 2 */
  if ((((ix - 0x3ff00000) | lx) == 0) || (((ix - 0x40000000) | lx) == 0))
    r = 0;
  /* for x < 2.0 */
  else if (ix < 0x40000000) {
    if (ix <= 0x3feccccc) { /* lgamma(x) = lgamma(x+1)-log(x) */
      r = -math::log(x);
      if (ix >= 0x3FE76944) {
        y = one - x;
        i = 0;
      } else if (ix >= 0x3FCDA661) {
        y = x - (tc - one);
        i = 1;
      } else {
        y = x;
        i = 2;
      }
    } else {
      r = zero;
      if (ix >= 0x3FFBB4C3) {
        y = 2.0 - x;
        i = 0;
      } /* [1.7316,2] */
      else if (ix >= 0x3FF3B4C4) {
        y = x - tc;
        i = 1;
      } /* [1.23,1.73] */
      else {
        y = x - one;
        i = 2;
      }
    }
    switch (i) {
    case 0:
      z = y * y;
      p1 = a0 + z * (a2 + z * (a4 + z * (a6 + z * (a8 + z * a10))));
      p2 = z * (a1 + z * (a3 + z * (a5 + z * (a7 + z * (a9 + z * a11)))));
      p = y * p1 + p2;
      r += (p - 0.5 * y);
      break;
    case 1:
      z = y * y;
      w = z * y;
      p1 = t0 + w * (t3 + w * (t6 + w * (t9 + w * t12))); /* parallel comp */
      p2 = t1 + w * (t4 + w * (t7 + w * (t10 + w * t13)));
      p3 = t2 + w * (t5 + w * (t8 + w * (t11 + w * t14)));
      p = z * p1 - (tt - w * (p2 + y * p3));
      r += (tf + p);
      break;
    case 2:
      p1 = y * (u0 + y * (u1 + y * (u2 + y * (u3 + y * (u4 + y * u5)))));
      p2 = one + y * (v1 + y * (v2 + y * (v3 + y * (v4 + y * v5))));
      r += (-0.5 * y + p1 / p2);
    }
  } else if (ix < 0x40200000) { /* x < 8.0 */
    i = static_cast<int>(x);
    y = x - static_cast<double>(i);
    p = y *
        (s0 + y * (s1 + y * (s2 + y * (s3 + y * (s4 + y * (s5 + y * s6))))));
    q = one + y * (r1 + y * (r2 + y * (r3 + y * (r4 + y * (r5 + y * r6)))));
    r = half * y + p / q;
    z = one; /* lgamma(1+s) = log(s) + lgamma(s) */
    switch (i) {
    case 7:
      z *= (y + 6.0);
      [[fallthrough]];
    case 6:
      z *= (y + 5.0);
      [[fallthrough]];
    case 5:
      z *= (y + 4.0);
      [[fallthrough]];
    case 4:
      z *= (y + 3.0);
      [[fallthrough]];
    case 3:
      z *= (y + 2.0);
      r += math::log(z);
      break;
    }
    /* 8.0 <= x < 2**58 */
  } else if (ix < 0x43900000) {
    t = math::log(x);
    z = one / x;
    y = z * z;
    w = w0 + z * (w1 + y * (w2 + y * (w3 + y * (w4 + y * (w5 + y * w6)))));
    r = (x - half) * (t - one) + w;
  } else
    /* 2**58 <= x <= inf */
    r = x * (math::log(x) - one);

  return r;
}

// Called only for nonintegral negative inputs with |x| < 2^52.
// Reduce before multiplying by pi, including very close to a pole.
LIBC_INLINE double reflection_sine(double x, int &sign) {
  double a = -x;
  double n = fputil::floor(a);
  double f = a - n;
  sign = (static_cast<uint64_t>(n) & 1) ? 1 : -1;
  if (f > 0.5)
    f = 1.0 - f;
  constexpr double PI = 0x1.921fb54442d18p+1;
  return f <= 0.25 ? math::sin(PI * f) : math::cos(PI * (0.5 - f));
}

template <typename T> LIBC_INLINE T overflow(Sign sign = Sign::POS) {
  using Bits = fputil::FPBits<T>;
  fputil::set_errno_if_required(ERANGE);
  fputil::raise_except_if_required(FE_OVERFLOW | FE_INEXACT);
  int mode = fputil::quick_get_round();
  if (mode == FE_TOWARDZERO || (mode == FE_DOWNWARD && sign == Sign::POS) ||
      (mode == FE_UPWARD && sign == Sign::NEG))
    return Bits::max_normal(sign).get_val();
  return Bits::inf(sign).get_val();
}

template <typename T> LIBC_INLINE T underflow(Sign sign) {
  using Bits = fputil::FPBits<T>;
  fputil::set_errno_if_required(ERANGE);
  fputil::raise_except_if_required(FE_UNDERFLOW | FE_INEXACT);
  int mode = fputil::quick_get_round();
  if ((mode == FE_UPWARD && sign == Sign::POS) ||
      (mode == FE_DOWNWARD && sign == Sign::NEG))
    return Bits::min_subnormal(sign).get_val();
  return Bits::zero(sign).get_val();
}

} // namespace gamma_double_internal

// signp must be non-null. At negative integer poles the sign is chosen as +1;
// at signed zero it agrees with the corresponding one-sided gamma limit.
LIBC_INLINE double lgamma_r(double x, int *signp) {
  using Bits = fputil::FPBits<double>;
  using namespace gamma_double_internal;
  Bits bits(x);
  *signp = 1;
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  if (bits.is_inf())
    return Bits::inf().get_val();
  if (bits.is_zero() || (bits.is_neg() && gamma_internal::is_integer(x))) {
    if (bits.is_zero() && bits.is_neg())
      *signp = -1;
    fputil::set_errno_if_required(ERANGE);
    fputil::raise_except_if_required(FE_DIVBYZERO);
    return Bits::inf().get_val();
  }
  double a = bits.abs().get_val();
  if (a < 0x1p-70) {
    *signp = bits.is_neg() ? -1 : 1;
    return -math::log(a);
  }
  if (bits.is_neg()) {
    if (a <= 200.0)
      return log_gamma_recurrence(x, *signp);
    double s = reflection_sine(x, *signp);
    return math::log(0x1.921fb54442d18p+1 / (a * s)) - positive_log_gamma(a);
  }
  // Only the leading Stirling term matters here. Check before multiplying,
  // so overflow also sets errno under downward/toward-zero rounding.
  if (x >= 0x1p58) {
    double t = math::log(x) - 1.0;
    if (x > Bits::max_normal().get_val() / t)
      return overflow<double>();
    double r = x * t;
    if (Bits(r).is_inf())
      return overflow<double>();
    return r;
  }
  return positive_log_gamma(x);
}

// C17 does not require the POSIX signgam global. Match the existing lgammaf
// provider: callers needing the sign use lgamma_r.
LIBC_INLINE double lgamma(double x) {
  int sign;
  return math::lgamma_r(x, &sign);
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_LGAMMA_H
