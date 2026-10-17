//===-- Complex inverse kernels ---------------------------------*- C++ -*-===//
// SPDX-License-Identifier: BSD-2-Clause
//
// Adapted from FreeBSD lib/msun/src/catrig.c.
// Copyright (c) 2012 Stephen Montgomery-Smith <stephen@FreeBSD.ORG>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

#ifndef LLVM_LIBC_SRC___SUPPORT_COMPLEX_INVERSE_H
#define LLVM_LIBC_SRC___SUPPORT_COMPLEX_INVERSE_H

#include "src/__support/complex_transcendental.h"
#include "src/math/acos.h"
#include "src/math/asin.h"
#include "src/math/atan.h"
#include "src/math/atanh.h"

namespace LIBC_NAMESPACE_DECL {
namespace complex_detail {

LIBC_INLINE_VAR constexpr double PI_2 = 0x1.921fb54442d18p+0;
LIBC_INLINE_VAR constexpr double PI_2_LO = 0x1.1a62633145c07p-54;
LIBC_INLINE_VAR constexpr double LN_2 = 0x1.62e42fefa39efp-1;

// Rationalized (hypot(a,b)-b)/2. Callers select asymptotic formulas when
// a*a could underflow. Here a,b are bounded above by 2^52.
LIBC_INLINE double inverse_f(double a, double b, double h) {
  if (b < 0.0)
    return (h - b) * 0.5;
  if (b == 0.0)
    return a * 0.5;
  return (a * a / (h + b)) * 0.5;
}

struct InverseWork {
  double real;
  double b;
  double denominator;
  double numerator;
  bool use_b;
};

// Hull, Fairgrieve and Tang (ACM TOMS 23(3), 1997), as implemented by
// Montgomery-Smith: recover A-1 and A-y without subtracting rounded hypots.
// x,y >= 0, x,y <= 2^52. Output describes asinh(x+i*y) in the first quadrant.
LIBC_INLINE InverseWork inverse_work(double x, double y) {
  double r = LIBC_NAMESPACE::hypot(x, y + 1.0);
  double s = LIBC_NAMESPACE::hypot(x, y - 1.0);
  double a = (r + s) * 0.5;
  if (a < 1.0)
    a = 1.0;
  InverseWork w{};
  if (a < 10.0) {
    if (y == 1.0 && x < 0x1p-111) {
      w.real = LIBC_NAMESPACE::sqrt(x);
    } else if (x >= 0x1p-52 * abs(y - 1.0)) {
      double am1 = inverse_f(x, 1.0 + y, r) + inverse_f(x, 1.0 - y, s);
      w.real =
          LIBC_NAMESPACE::log1p(am1 + LIBC_NAMESPACE::sqrt(am1 * (a + 1.0)));
    } else if (y < 1.0) {
      w.real = x / LIBC_NAMESPACE::sqrt((1.0 - y) * (1.0 + y));
    } else {
      w.real = LIBC_NAMESPACE::log1p(
          (y - 1.0) + LIBC_NAMESPACE::sqrt((y - 1.0) * (y + 1.0)));
    }
  } else {
    w.real = LIBC_NAMESPACE::log(a + LIBC_NAMESPACE::sqrt(a * a - 1.0));
  }
  w.numerator = y;
  if (y < 0x1p-509) {
    w.denominator = a * 0x1p53;
    w.numerator = y * 0x1p53;
    return w;
  }
  w.b = y / a;
  w.use_b = w.b <= 0.6417;
  if (!w.use_b) {
    if (y == 1.0 && x < 0x1p-59) {
      w.denominator =
          LIBC_NAMESPACE::sqrt(x) * LIBC_NAMESPACE::sqrt((a + y) * 0.5);
    } else if (x >= 0x1p-52 * abs(y - 1.0)) {
      double amy = inverse_f(x, y + 1.0, r) + inverse_f(x, y - 1.0, s);
      w.denominator = LIBC_NAMESPACE::sqrt(amy * (a + y));
    } else if (y > 1.0) {
      w.denominator =
          x * 0x1p106 * y / LIBC_NAMESPACE::sqrt((y + 1.0) * (y - 1.0));
      w.numerator = y * 0x1p106;
    } else {
      w.denominator = LIBC_NAMESPACE::sqrt((1.0 - y) * (1.0 + y));
    }
  }
  return w;
}

LIBC_INLINE Pair asinh(Pair z) {
  double x = z.real, y = z.imag, ax = abs(x), ay = abs(y);
  if (Bits(x).is_nan() || Bits(y).is_nan()) {
    if (Bits(x).is_inf())
      return {x, y + y};
    if (Bits(y).is_inf())
      return {y, x + x};
    if (y == 0.0)
      return {x + x, y};
    return {x + y, x + y};
  }
  if (ax > 0x1p52 || ay > 0x1p52)
    return {LIBC_NAMESPACE::copysign(log_abs(x, y) + LN_2, x),
            LIBC_NAMESPACE::copysign(LIBC_NAMESPACE::atan2(ay, ax), y)};
  if (ax < 0x1p-27 && ay < 0x1p-27)
    return z;
  auto w = inverse_work(ax, ay);
  double im = w.use_b ? LIBC_NAMESPACE::asin(w.b)
                      : LIBC_NAMESPACE::atan2(w.numerator, w.denominator);
  return {LIBC_NAMESPACE::copysign(w.real, x), LIBC_NAMESPACE::copysign(im, y)};
}

LIBC_INLINE Pair asin(Pair z) {
  auto w = asinh({z.imag, z.real});
  return {w.imag, w.real};
}

LIBC_INLINE Pair acos(Pair z) {
  double x = z.real, y = z.imag, ax = abs(x), ay = abs(y);
  if (Bits(x).is_nan() || Bits(y).is_nan()) {
    if (Bits(x).is_inf())
      return {y + y, -inf()};
    if (Bits(y).is_inf())
      return {x + x, -y};
    if (x == 0.0)
      return {PI_2 + PI_2_LO, y + y};
    return {x + y, x + y};
  }
  if (ax > 0x1p52 || ay > 0x1p52)
    return {LIBC_NAMESPACE::atan2(ay, x),
            -LIBC_NAMESPACE::copysign(log_abs(x, y) + LN_2, y)};
  if (x == 1.0 && y == 0.0)
    return {0.0, -y};
  if (ax < 0x1p-27 && ay < 0x1p-27)
    return {PI_2 - (x - PI_2_LO), -y};
  auto w = inverse_work(ay, ax);
  double re =
      w.use_b ? LIBC_NAMESPACE::acos(LIBC_NAMESPACE::copysign(w.b, x))
              : LIBC_NAMESPACE::atan2(w.denominator,
                                      LIBC_NAMESPACE::copysign(w.numerator, x));
  return {re, -LIBC_NAMESPACE::copysign(w.real, y)};
}

LIBC_INLINE Pair acosh(Pair z) {
  auto w = acos(z);
  if (Bits(w.real).is_nan())
    return {abs(w.imag), w.real};
  if (Bits(w.imag).is_nan())
    return {w.imag, w.imag};
  return {abs(w.imag), LIBC_NAMESPACE::copysign(w.real, z.imag)};
}

// Real part of 1/z for max(|x|,|y|)>2^52. Normalize both components to
// avoid squaring overflow; widely separated exponents use division directly.
LIBC_INLINE double inverse_real_reciprocal(double x, double y) {
  double ax = abs(x), ay = abs(y);
  if (Bits(x).is_inf() || Bits(y).is_inf())
    return LIBC_NAMESPACE::copysign(0.0, x);
  if (ax >= ay && ay / ax < 0x1p-27)
    return 1.0 / x;
  if (ay > ax && ax / ay < 0x1p-27)
    return (x / ay) / ay;
  int e;
  LIBC_NAMESPACE::frexp(ax > ay ? ax : ay, &e);
  double a = LIBC_NAMESPACE::ldexp(x, -e);
  double b = LIBC_NAMESPACE::ldexp(y, -e);
  return LIBC_NAMESPACE::ldexp(a / (a * a + b * b), -e);
}

LIBC_INLINE Pair atanh(Pair z) {
  double x = z.real, y = z.imag, ax = abs(x), ay = abs(y);
  if (y == 0.0 && ax <= 1.0)
    return {LIBC_NAMESPACE::atanh(x), y};
  if (x == 0.0)
    return {x, LIBC_NAMESPACE::atan(y)};
  if (Bits(x).is_nan() || Bits(y).is_nan()) {
    if (Bits(x).is_inf())
      return {LIBC_NAMESPACE::copysign(0.0, x), y + y};
    if (Bits(y).is_inf())
      return {LIBC_NAMESPACE::copysign(0.0, x),
              LIBC_NAMESPACE::copysign(PI_2 + PI_2_LO, y)};
    return {x + y, x + y};
  }
  if (ax > 0x1p52 || ay > 0x1p52)
    return {inverse_real_reciprocal(x, y),
            LIBC_NAMESPACE::copysign(PI_2 + PI_2_LO, y)};
  if (ax < 0x1p-27 && ay < 0x1p-27)
    return z;
  double re;
  if (ax == 1.0 && ay < 0x1p-52) {
    re = (LN_2 - LIBC_NAMESPACE::log(ay)) * 0.5;
  } else {
    double d = (ax - 1.0) * (ax - 1.0);
    if (ay >= 0x1p-511)
      d += ay * ay;
    re = 0.25 * LIBC_NAMESPACE::log1p(4.0 * ax / d);
  }
  double im;
  if (ax == 1.0)
    im = 0.5 * LIBC_NAMESPACE::atan2(2.0, -ay);
  else {
    double d = (1.0 - ax) * (1.0 + ax);
    if (ay >= 0x1p-52)
      d -= ay * ay;
    im = 0.5 * LIBC_NAMESPACE::atan2(2.0 * ay, d);
  }
  return {LIBC_NAMESPACE::copysign(re, x), LIBC_NAMESPACE::copysign(im, y)};
}

LIBC_INLINE Pair atan(Pair z) {
  auto w = atanh({z.imag, z.real});
  return {w.imag, w.real};
}

} // namespace complex_detail
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_COMPLEX_INVERSE_H
