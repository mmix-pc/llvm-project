//===-- Scaled integer-order Bessel composition -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_INTEGER_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_INTEGER_H

#include "src/__support/math/bessel.h"
#include "src/__support/math/bessel_hankel.h"
#include "src/__support/math/bessel_recurrence.h"
#include "src/__support/math/bessel_uniform.h"
#include "src/__support/math/exp.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

struct IntegerBesselPair {
  BesselFloat j;
  BesselFloat y;
  int64_t j_scale;
  int64_t y_scale;
};

// Internal RN composition for 2 <= n <= 2^31 and finite x > 0. Neither the
// recurrence nor the exponential scale is rounded to the public range here.
LIBC_INLINE IntegerBesselPair bessel_integer(unsigned n, double x) {
  double order = double(n);
  double boundary = 16.0 * order * order;
  if (x >= (boundary > 128.0 ? boundary : 128.0)) {
    auto pair = bessel_hankel(n, x);
    return {BesselFloat(pair.j), BesselFloat(pair.y), 0, 0};
  }
  if (n <= 2048) {
    auto pair = bessel_recurrence(n, x);
    return {pair.j, pair.y, 0, 0};
  }
  auto pair = bessel_uniform(order, x);
  if (!pair.scaled)
    return {BesselFloat(pair.j), BesselFloat(pair.y), 0, 0};

  using LIBC_NAMESPACE::operator""_u128;
  constexpr BesselFloat LN2(Sign::POS, -128,
                            0xb17217f7'd1cf79ab'c9e3b398'03f2f6af_u128);
  // phase < n*log(2*n/x) < 2^41 in this domain, even for minimum x.
  // The exponent needs 64 bits; the residual, not phase, is sent to exp.
  int64_t exponent =
      int64_t(double(bessel_phase_internal::divide(pair.phase, LN2)));
  auto residual = fputil::quick_sub(
      pair.phase, fputil::quick_mul(BesselFloat(double(exponent)), LN2));
  double factor = math::exp(double(residual));
  return {fputil::quick_mul(BesselFloat(pair.j),
                            fputil::approx_reciprocal(BesselFloat(factor))),
          fputil::quick_mul(BesselFloat(pair.y), BesselFloat(factor)),
          -exponent, exponent};
}

// Apply the sign before directed rounding. Clamping the exponent here only
// selects a provably out-of-binary64-range conversion; it does not cap work
// or invent a finite kernel result. Call with the caller's rounding restored.
LIBC_INLINE double bessel_integer_result(BesselFloat value, int64_t scale,
                                         bool negate) {
  if (negate)
    value = -value;
  int64_t exponent = int64_t(value.exponent) + scale;
  value.exponent = int(exponent > 4096    ? 4096
                       : exponent < -4096 ? -4096
                                          : exponent);
  return value.generic_as<double, /*ShouldSignalExceptions=*/true>();
}

template <bool SecondKind>
LIBC_INLINE double integer_bessel(int signed_order, double x) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  unsigned n =
      signed_order < 0 ? 0U - unsigned(signed_order) : unsigned(signed_order);
  bool negate =
      (n & 1) && ((signed_order < 0) != (!SecondKind && bits.is_neg()));
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  if constexpr (SecondKind) {
    if (bits.is_zero()) {
      fputil::set_errno_if_required(ERANGE);
      fputil::raise_except_if_required(FE_DIVBYZERO);
      return Bits::inf(negate ? Sign::POS : Sign::NEG).get_val();
    }
    if (bits.is_neg()) {
      fputil::set_errno_if_required(EDOM);
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
  }
  if (bits.is_inf())
    return Bits::zero(negate ? Sign::NEG : Sign::POS).get_val();
  if (bits.is_zero())
    return n == 0 ? 1.0 : Bits::zero(negate ? Sign::NEG : Sign::POS).get_val();
  if (n == 0)
    return bessel<0, SecondKind>(x);
  if constexpr (!SecondKind) {
    if (n == 1)
      return bessel<1, false>(signed_order < 0 ? -x : x);
  }

  // Kernels have an RN accuracy contract. Hide their intermediate flags and
  // errno, restore the complete caller environment, then apply the sign and
  // round the final scaled result in the caller's mode.
  fenv_t environment;
  fputil::get_env(&environment);
  int saved_errno = libc_errno;
  fputil::disable_except(FE_ALL_EXCEPT);
  fputil::clear_except(FE_ALL_EXCEPT);
  fputil::set_round(FE_TONEAREST);
  bits.set_sign(Sign::POS);
  double argument = bits.get_val();
  BesselFloat value;
  int64_t scale = 0;
  if (n == 1) {
    value = argument < 0x1p-54
                ? fputil::quick_mul(
                      BesselFloat(-bessel1_internal::tpi),
                      fputil::approx_reciprocal(BesselFloat(argument)))
                : BesselFloat(bessel1_internal::y1(argument));
  } else {
    auto pair = bessel_integer(n, argument);
    value = SecondKind ? pair.y : pair.j;
    scale = SecondKind ? pair.y_scale : pair.j_scale;
  }
  fputil::set_env(&environment);
  libc_errno = saved_errno;
  return bessel_integer_result(value, scale, negate);
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_INTEGER_H
