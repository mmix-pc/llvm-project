//===-- Range-safe complex power --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_COMPLEX_POWER_H
#define LLVM_LIBC_SRC___SUPPORT_COMPLEX_POWER_H

#include "src/__support/FPUtil/dyadic_float.h"
#include "src/__support/complex_transcendental.h"
#include "src/__support/math/sincos_integer_utils.h"
#include "src/math/pow.h"
#include "src/math/remainder.h"

namespace LIBC_NAMESPACE_DECL {
namespace complex_detail {

using PowerFloat = fputil::DyadicFloat<128>;

// IEEE addition of exact zeros: opposite signs cancel to +0 except under
// downward rounding. Do not inherit quick_add's return-the-other-zero shortcut.
LIBC_INLINE Sign power_zero_sum(Sign a, Sign b) {
  if (a == b)
    return a;
#ifndef LIBC_MATH_HAS_ASSUME_ROUND_NEAREST_ONLY
  if (fputil::quick_get_round() == FE_DOWNWARD)
    return Sign::NEG;
#endif
  return Sign::POS;
}

LIBC_INLINE PowerFloat power_add(const PowerFloat &a, const PowerFloat &b) {
  auto sum = fputil::quick_add(a, b);
  if (sum.mantissa.is_zero())
    sum.sign = power_zero_sum(a.sign, b.sign);
  return {sum.sign, sum.exponent, sum.mantissa};
}

LIBC_INLINE PowerFloat power_sub(const PowerFloat &a, const PowerFloat &b) {
  PowerFloat negative_b(b.sign.negate(), b.exponent, b.mantissa);
  return power_add(a, negative_b);
}

struct PowerPair {
  PowerFloat real;
  PowerFloat imag;

  // Construct from fields rather than direct-initializing a DyadicFloat from
  // another DyadicFloat, which considers its unconstrained conversion operator.
  LIBC_INLINE PowerPair(const PowerFloat &re, const PowerFloat &im)
      : real(re.sign, re.exponent, re.mantissa),
        imag(im.sign, im.exponent, im.mantissa) {}

  LIBC_INLINE PowerPair(const PowerPair &other)
      : PowerPair(other.real, other.imag) {}

  LIBC_INLINE PowerPair &operator=(const PowerPair &other) {
    real.sign = other.real.sign;
    real.exponent = other.real.exponent;
    real.mantissa = other.real.mantissa;
    imag.sign = other.imag.sign;
    imag.exponent = other.imag.exponent;
    imag.mantissa = other.imag.mantissa;
    return *this;
  }
};

LIBC_INLINE PowerPair power_mul(const PowerPair &a, const PowerPair &b) {
  return {power_sub(fputil::quick_mul(a.real, b.real),
                    fputil::quick_mul(a.imag, b.imag)),
          power_add(fputil::quick_mul(a.real, b.imag),
                    fputil::quick_mul(a.imag, b.real))};
}

// Small integral powers avoid the logarithm altogether. The software exponent
// is unbounded over this input range; reciprocal powers never overflow before
// the reciprocal is taken. Only the final conversions can overflow/underflow.
LIBC_INLINE Pair power_integer(Pair z, int n) {
  PowerPair a{PowerFloat(z.real), PowerFloat(z.imag)};
  PowerPair r{PowerFloat(1.0), PowerFloat(0.0)};
  unsigned k = n < 0 ? unsigned(-n) : unsigned(n);
  while (k) {
    if (k & 1)
      r = power_mul(r, a);
    k >>= 1;
    if (k)
      a = power_mul(a, a);
  }
  if (n < 0) {
    auto d = fputil::quick_add(fputil::quick_mul(r.real, r.real),
                               fputil::quick_mul(r.imag, r.imag));
    auto inv = fputil::approx_reciprocal(d);
    r = {fputil::quick_mul(r.real, inv), -fputil::quick_mul(r.imag, inv)};
  }
  return {r.real.as<double, true>(), r.imag.as<double, true>()};
}

// Unit vector for pi*t, without forming pi*t for a huge integral t. Reduce
// before multiplication and represent the exact axis values explicitly.
LIBC_INLINE Pair power_pi_phase(double t) {
  double r = LIBC_NAMESPACE::remainder(t, 2.0);
  double a = abs(r);
  if (a == 0.0)
    return {1.0, -LIBC_NAMESPACE::copysign(0.0, t)};
  if (a == 1.0)
    return {-1.0, LIBC_NAMESPACE::copysign(0.0, t)};
  if (a == 0.5)
    // At half-integral powers retain the one-sided real-component limit:
    // cos(pi*t - sign(t)*epsilon) has sign sign(t)*sin(pi*t).
    return {Bits(r).is_neg() == Bits(t).is_neg() ? 0.0 : -0.0,
            LIBC_NAMESPACE::copysign(1.0, r)};
  bool flip = a > 0.5;
  if (flip)
    a = 1.0 - a;
  double angle =
      LIBC_NAMESPACE::fma(a, 0x1.921fb54442d18p+1, a * 0x1.1a62633145c07p-53);
  return {flip ? -LIBC_NAMESPACE::cos(angle) : LIBC_NAMESPACE::cos(angle),
          LIBC_NAMESPACE::copysign(LIBC_NAMESPACE::sin(angle), r)};
}

// Reduce each exact 53-bit chunk of a software-precision phase with LLVM's
// integer Payne-Hanek reducer. Its 1280-bit 2/pi table covers exponent 1035,
// the maximum exponent of w*log(z) for finite binary64 inputs. In particular,
// no binary64 phase overflow, rounded 2*pi remainder, or angle doubling occurs.
LIBC_INLINE Pair power_phase(const PowerFloat &input) {
  using namespace math::integer_only;
  PowerFloat phase(input.sign, input.exponent, input.mantissa);
  PowerFloat residual(0.0);
  unsigned quadrant = 0;
  while (!phase.mantissa.is_zero()) {
    int e = phase.get_unbiased_exponent();
    if (e < 0) {
      residual = fputil::quick_add(residual, phase);
      break;
    }
    auto m = phase.mantissa;
    m >>= 75;
    uint64_t chunk = static_cast<uint64_t>(m);
    PowerFloat part(phase.sign, e - 52, PowerFloat::MantissaType(chunk));
    phase = fputil::quick_sub(phase, part);
    unsigned k;
    Frac128 frac;
    bool negative =
        trig_range_reduction(chunk, unsigned(e + Bits::EXP_BIAS), k, frac);
    if (part.sign.is_neg()) {
      k = 0U - k;
      negative = !negative;
    }
    quadrant += k;
    PowerFloat tail(negative ? Sign::NEG : Sign::POS, -128,
                    PowerFloat::MantissaType(frac.val));
    residual = fputil::quick_add(residual, tail);
  }
  double r = static_cast<double>(residual);
  double c = LIBC_NAMESPACE::cos(r), s = LIBC_NAMESPACE::sin(r);
  switch (quadrant & 3U) {
  case 0:
    return {c, s};
  case 1:
    return {-s, c};
  case 2:
    return {-c, -s};
  default:
    return {s, -c};
  }
}

LIBC_INLINE Pair power_exp(const PowerFloat &magnitude, Pair phase) {
  // Saturate only beyond the range that ANY binary64 trigonometric factor
  // could rescue. Avoid converting a huge finite software value to infinity.
  double r;
  if (!magnitude.mantissa.is_zero() && magnitude.get_unbiased_exponent() > 10)
    r = magnitude.sign.is_neg() ? -1601.0 : 1601.0;
  else
    r = static_cast<double>(magnitude);
  return {exp_component(r, phase.real), exp_component(r, phase.imag)};
}

LIBC_INLINE Pair power_exp_phase(const PowerFloat &magnitude,
                                 const PowerFloat &angle) {
  if (!angle.mantissa.is_zero() && angle.get_unbiased_exponent() < -26) {
    // A phase below the binary64 range can still give a representable imaginary
    // result after exponential scaling. Keep its exponent outside binary64.
    int e = angle.get_unbiased_exponent();
    PowerFloat m(angle.sign, angle.exponent, angle.mantissa);
    m.exponent -= e;
    PowerFloat shift = fputil::quick_mul(PowerFloat(double(e)),
                                         PowerFloat(0x1.62e42fefa39efp-1));
    shift = fputil::quick_add(
        shift, fputil::quick_mul(PowerFloat(double(e)),
                                 PowerFloat(0x1.abc9e3b39803fp-56)));
    auto im = power_exp(fputil::quick_add(magnitude, shift),
                        {static_cast<double>(m), 0.0});
    return {power_exp(magnitude, {1.0, 0.0}).real, im.real};
  }
  return power_exp(magnitude, power_phase(angle));
}

LIBC_INLINE Pair pow(Pair z, Pair w) {
  double x = z.real, y = z.imag, u = w.real, v = w.imag;
  if (u == 0.0 && v == 0.0)
    return {1.0, 0.0};
  if (u == 1.0 && v == 0.0)
    return z;
  if (u == 0.5 && v == 0.0)
    return sqrt(z);
  if (x == 1.0 && y == 0.0)
    return {1.0, 0.0};
  if (x >= 0.0 && y == 0.0 && v == 0.0 && Bits(u).is_inf())
    return {LIBC_NAMESPACE::pow(x, u), LIBC_NAMESPACE::copysign(0.0, y)};
  bool finite_z = Bits(x).is_finite() && Bits(y).is_finite();
  bool finite_w = Bits(u).is_finite() && Bits(v).is_finite();
  if (y == 0.0 && v == 0.0 && Bits(u).is_finite() && !Bits(x).is_nan()) {
    Pair phase = Bits(x).is_neg()
                     ? power_pi_phase(Bits(y).is_neg() ? -u : u)
                     : Pair{1.0, LIBC_NAMESPACE::copysign(0.0, y * u)};
    if (x != 0.0 && Bits(x).is_finite() && phase.imag == 0.0) {
      // For an exactly real result F=x^u, our zero convention accounts for
      // BOTH imaginary inputs: dIm(F) = (u*F/x)*dy + F*log(|x|)*dv.
      // Evaluate the signs of these zero terms without overflowing their
      // coefficients. Opposite zero contributions use IEEE cancellation.
      // Thus (-2+i0)^(2+i0) has (-0)+(+0) = +0 in round-to-nearest,
      // unlike perturbing only the base. C17 does not prescribe a unique
      // cpow zero-sign rule here; this is an explicit library convention.
      bool negative_y = Bits(u).is_neg() ^ Bits(phase.real).is_neg() ^
                        Bits(x).is_neg() ^ Bits(y).is_neg();
      Sign sign = negative_y ? Sign::NEG : Sign::POS;
      if (abs(x) != 1.0) {
        bool negative_v =
            Bits(phase.real).is_neg() ^ (abs(x) < 1.0) ^ Bits(v).is_neg();
        sign = power_zero_sum(sign, negative_v ? Sign::NEG : Sign::POS);
      }
      phase.imag = Bits::zero(sign).get_val();
    }
    if (x != 0.0 && Bits(x).is_finite()) {
      auto magnitude = fputil::quick_mul(
          PowerFloat(u), PowerFloat(LIBC_NAMESPACE::log(abs(x))));
      if ((!magnitude.mantissa.is_zero() &&
           magnitude.get_unbiased_exponent() > 9) &&
          (magnitude.get_unbiased_exponent() > 10 ||
           abs(static_cast<double>(magnitude)) > 700.0))
        return power_exp(magnitude, phase);
    }
    // Real pow provides an accurate magnitude on the axis. Zero phase
    // components must not be multiplied by an infinite magnitude.
    double r = LIBC_NAMESPACE::pow(abs(x), u);
    if (Bits(r).is_inf()) {
      // If the real magnitude overflowed, a nonzero small phase component
      // may still be finite. Recompute using the scaled exponential.
      if (x != 0.0 && Bits(x).is_finite())
        return power_exp(
            fputil::quick_mul(PowerFloat(u),
                              PowerFloat(LIBC_NAMESPACE::log(abs(x)))),
            phase);
      return {phase.real == 0.0 ? phase.real
                                : LIBC_NAMESPACE::copysign(r, phase.real),
              phase.imag == 0.0 ? phase.imag
                                : LIBC_NAMESPACE::copysign(r, phase.imag)};
    }
    return {r * phase.real, r * phase.imag};
  }
  if (!finite_z && finite_w && v == 0.0 && !Bits(x).is_nan() &&
      !Bits(y).is_nan()) {
    auto phase = power_phase(fputil::quick_mul(
        PowerFloat(u), PowerFloat(LIBC_NAMESPACE::atan2(y, x))));
    double r = u < 0.0 ? 0.0 : inf();
    return {phase.real == 0.0 ? phase.real
                              : LIBC_NAMESPACE::copysign(r, phase.real),
            phase.imag == 0.0 ? phase.imag
                              : LIBC_NAMESPACE::copysign(r, phase.imag)};
  }
  if (!finite_z || !finite_w || (x == 0.0 && y == 0.0)) {
    // Non-real powers of zero/infinity have no limiting phase. Preserve the
    // real-axis cases above. This indeterminate branch returns quiet NaNs.
    double n = Bits::quiet_nan().get_val();
    return {n, n};
  }
  if (v == 0.0 && abs(u) <= 64.0 && u == double(static_cast<int>(u)))
    return power_integer(z, static_cast<int>(u));

  PowerFloat lr(log_abs(x, y));
  PowerFloat arg(LIBC_NAMESPACE::atan2(y, x));
  // log(1+i*y) ~ y*y/2 and arg(x+i*y) ~ y/x can lie below the binary64
  // range yet contribute to a representable result after multiplication by w.
  if (abs(x) == 1.0 && abs(y) < 0x1p-500) {
    lr = fputil::quick_mul(PowerFloat(y), PowerFloat(y));
    --lr.exponent;
  }
  if (x > 0.0 && y != 0.0 && abs(y) < x * 0x1p-500)
    arg = fputil::quick_mul(PowerFloat(y),
                            fputil::approx_reciprocal(PowerFloat(x)));
  PowerFloat real = fputil::quick_sub(fputil::quick_mul(PowerFloat(u), lr),
                                      fputil::quick_mul(PowerFloat(v), arg));
  if (x < 0.0 && y == 0.0) {
    // The real-axis phase is an exact multiple of pi, not u times a rounded
    // atan2 result. This matters even for integral u larger than 2^53.
    auto a = power_pi_phase(Bits(y).is_neg() ? -u : u);
    auto b = power_phase(fputil::quick_mul(PowerFloat(v), lr));
    Pair phase{LIBC_NAMESPACE::fma(a.real, b.real, -a.imag * b.imag),
               LIBC_NAMESPACE::fma(a.real, b.imag, a.imag * b.real)};
    return power_exp(real, phase);
  }
  PowerFloat imag = fputil::quick_add(fputil::quick_mul(PowerFloat(u), arg),
                                      fputil::quick_mul(PowerFloat(v), lr));
  return power_exp_phase(real, imag);
}

} // namespace complex_detail
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_COMPLEX_POWER_H
