//===-- Complex transcendental kernels --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception) AND BSD-2-Clause
//
//===----------------------------------------------------------------------===//

// The square-root rescaling is adapted from newlib/libm/complex/csqrt.c,
// NetBSD revision 1.1 (2007/08/20), imported into newlib by Marco Atzeri.
//
// Copyright (c) 2007 The NetBSD Foundation, Inc.
// All rights reserved.
//
// This code is derived from software written by Stephen L. Moshier.
// It is redistributed by the NetBSD Foundation by permission of the author.
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
// THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
// ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
// BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef LLVM_LIBC_SRC___SUPPORT_COMPLEX_TRANSCENDENTAL_H
#define LLVM_LIBC_SRC___SUPPORT_COMPLEX_TRANSCENDENTAL_H

#include "src/__support/CPP/bit.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/complex_type.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include "src/math/atan2.h"
#include "src/math/copysign.h"
#include "src/math/cos.h"
#include "src/math/cosh.h"
#include "src/math/exp.h"
#include "src/math/fma.h"
#include "src/math/frexp.h"
#include "src/math/hypot.h"
#include "src/math/ldexp.h"
#include "src/math/log.h"
#include "src/math/log1p.h"
#include "src/math/sin.h"
#include "src/math/sinh.h"
#include "src/math/sqrt.h"
#include "src/math/tan.h"
#include "src/math/tanh.h"

namespace LIBC_NAMESPACE_DECL {
namespace complex_detail {

using Bits = fputil::FPBits<double>;
using Pair = Complex<double>;

LIBC_INLINE double abs(double x) { return Bits(x).abs().get_val(); }
LIBC_INLINE double inf() { return Bits::inf().get_val(); }

template <typename T> LIBC_INLINE Pair unpack(make_complex_t<T> z) {
  auto p = cpp::bit_cast<Complex<T>>(z);
  return {double(p.real), double(p.imag)};
}

template <typename T> LIBC_INLINE make_complex_t<T> pack(Pair z) {
  return cpp::bit_cast<make_complex_t<T>>(
      Complex<T>{static_cast<T>(z.real), static_cast<T>(z.imag)});
}

LIBC_INLINE Pair sqrt(Pair z) {
  double x = z.real, y = z.imag;
  Bits xb(x), yb(y);
  if (yb.is_inf())
    return {inf(), y};
  if (xb.is_inf()) {
    if (xb.is_neg())
      return {yb.is_nan() ? y + y : 0.0, LIBC_NAMESPACE::copysign(inf(), y)};
    return {x, yb.is_nan() ? y + y : LIBC_NAMESPACE::copysign(0.0, y)};
  }
  if (xb.is_nan() || yb.is_nan()) {
    double n = x + y;
    return {n, n};
  }
  if (y == 0.0) {
    double t = LIBC_NAMESPACE::sqrt(abs(x));
    return x < 0.0 ? Pair{0.0, LIBC_NAMESPACE::copysign(t, y)} : Pair{t, y};
  }

  // Form the larger component with a scaled hypot. Recover the smaller
  // component from the ORIGINAL y, so scaling cannot erase a tiny component.
  double a = abs(x), b = abs(y), scale;
  if (a > 4.0 || b > 4.0) {
    a *= 0.25;
    b *= 0.25;
    scale = 2.0;
  } else {
    a *= 0x1p54;
    b *= 0x1p54;
    scale = 0x1p-27;
  }
  double t =
      scale * LIBC_NAMESPACE::sqrt(0.5 * LIBC_NAMESPACE::hypot(a, b) + 0.5 * a);
  double u = abs(y) / (2.0 * t);
  return x < 0.0 ? Pair{u, LIBC_NAMESPACE::copysign(t, y)}
                 : Pair{t, LIBC_NAMESPACE::copysign(u, y)};
}

LIBC_INLINE double log_abs(double x, double y) {
  double a = abs(x), b = abs(y);
  if (Bits(a).is_inf() || Bits(b).is_inf())
    return inf();
  if (Bits(a).is_nan() || Bits(b).is_nan())
    return a + b;
  if (a < b) {
    double t = a;
    a = b;
    b = t;
  }
  if (b == 0.0)
    return LIBC_NAMESPACE::log(a);

  if (a >= 0.75 && a <= 1.25) {
    // Compensated squared norm: hypot can round to 1 even when log(|z|)
    // is nonzero. Keep the residual of both squares and their sum.
    double aa = a * a, bb = b * b;
    double s = aa + bb;
    double e = ((aa - s) + bb) + LIBC_NAMESPACE::fma(a, a, -aa);
    e += LIBC_NAMESPACE::fma(b, b, -bb);
    return 0.5 * LIBC_NAMESPACE::log1p((s - 1.0) + e);
  }
  // No overflowing norm, and no squaring of subnormal inputs.
  // Below this ratio the correction is negligible relative to log(a).
  if (a > 0x1p-970 && b < a * 0x1p-27)
    return LIBC_NAMESPACE::log(a);
  double r = b / a;
  return LIBC_NAMESPACE::log(a) + 0.5 * LIBC_NAMESPACE::log1p(r * r);
}

LIBC_INLINE Pair log(Pair z) {
  return {log_abs(z.real, z.imag), LIBC_NAMESPACE::atan2(z.imag, z.real)};
}

// exp(x) * t * 2^adjust, for finite x and |t| <= 1. Normalize t before
// multiplying, then do ONE final scaling, including subnormal results.
LIBC_INLINE double exp_component(double x, double t, int adjust = 0) {
  if (t == 0.0)
    return t;
  if (x > 1600.0)
    return LIBC_NAMESPACE::ldexp(t, 4096);
  if (x < -1600.0)
    return LIBC_NAMESPACE::ldexp(t, -4096);
  constexpr double LOG2_E = 0x1.71547652b82fep+0;
  constexpr double LN2_HI = 0x1.62e42fefa39efp-1;
  constexpr double LN2_LO = 0x1.abc9e3b39803fp-56;
  int k = static_cast<int>(x * LOG2_E);
  double r = LIBC_NAMESPACE::fma(-double(k), LN2_HI, x);
  r = LIBC_NAMESPACE::fma(-double(k), LN2_LO, r);
  int e;
  double m = LIBC_NAMESPACE::frexp(t, &e);
  return LIBC_NAMESPACE::ldexp(m * LIBC_NAMESPACE::exp(r), k + e + adjust);
}

LIBC_INLINE Pair exp(Pair z) {
  double x = z.real, y = z.imag;
  if (y == 0.0)
    return {LIBC_NAMESPACE::exp(x), y};
  Bits xb(x), yb(y);
  if (xb.is_inf() && xb.is_neg()) {
    if (!yb.is_finite())
      return {0.0, LIBC_NAMESPACE::copysign(0.0, y)};
    return {LIBC_NAMESPACE::copysign(0.0, LIBC_NAMESPACE::cos(y)),
            LIBC_NAMESPACE::copysign(0.0, LIBC_NAMESPACE::sin(y))};
  }
  if (!yb.is_finite()) {
    double n = y - y;
    return {xb.is_inf() ? inf() : n, n};
  }
  if (xb.is_nan())
    return {x + x, x + x};
  double c = LIBC_NAMESPACE::cos(y), s = LIBC_NAMESPACE::sin(y);
  if (xb.is_inf())
    return {LIBC_NAMESPACE::copysign(inf(), c),
            LIBC_NAMESPACE::copysign(inf(), s)};
  return {exp_component(x, c), exp_component(x, s)};
}

LIBC_INLINE Pair sinh(Pair z) {
  double x = z.real, y = z.imag;
  Bits xb(x), yb(y);
  if (y == 0.0)
    return {LIBC_NAMESPACE::sinh(x), y};
  if (!yb.is_finite()) {
    double n = y - y;
    return {x == 0.0 ? x : (xb.is_inf() ? x : n), n};
  }
  if (xb.is_nan())
    return {x + x, x + x};
  double s = LIBC_NAMESPACE::sin(y), c = LIBC_NAMESPACE::cos(y);
  if (xb.is_inf())
    return {LIBC_NAMESPACE::copysign(inf(), x) * c,
            LIBC_NAMESPACE::copysign(inf(), s)};
  double a = abs(x);
  if (a < 20.0)
    return {LIBC_NAMESPACE::sinh(x) * c, LIBC_NAMESPACE::cosh(x) * s};
  // exp(-a) changes either component by less than one ulp here.
  return {exp_component(a, xb.is_neg() ? -c : c, -1), exp_component(a, s, -1)};
}

LIBC_INLINE Pair cosh(Pair z) {
  double x = z.real, y = z.imag;
  Bits xb(x), yb(y);
  if (y == 0.0)
    return {LIBC_NAMESPACE::cosh(x), xb.is_neg() ? -y : y};
  if (!yb.is_finite()) {
    double n = y - y;
    return {xb.is_inf() ? inf() : n, x == 0.0 ? 0.0 : n};
  }
  if (xb.is_nan())
    return {x + x, x + x};
  double s = LIBC_NAMESPACE::sin(y), c = LIBC_NAMESPACE::cos(y);
  if (xb.is_inf())
    return {LIBC_NAMESPACE::copysign(inf(), c),
            LIBC_NAMESPACE::copysign(inf(), xb.is_neg() ? -s : s)};
  double a = abs(x);
  if (a < 20.0)
    return {LIBC_NAMESPACE::cosh(x) * c, LIBC_NAMESPACE::sinh(x) * s};
  return {exp_component(a, c, -1), exp_component(a, xb.is_neg() ? -s : s, -1)};
}

LIBC_INLINE Pair tanh(Pair z) {
  double x = z.real, y = z.imag;
  Bits xb(x), yb(y);
  if (xb.is_inf()) {
    double s =
        yb.is_finite() ? LIBC_NAMESPACE::sin(y) * LIBC_NAMESPACE::cos(y) : y;
    return {LIBC_NAMESPACE::copysign(1.0, x), LIBC_NAMESPACE::copysign(0.0, s)};
  }
  if (y == 0.0)
    return {LIBC_NAMESPACE::tanh(x), y};
  if (!yb.is_finite()) {
    double n = y - y;
    return {x == 0.0 ? x : n, n};
  }
  if (xb.is_nan())
    return {x + x, x + x};
  if (x == 0.0)
    return {x, LIBC_NAMESPACE::tan(y)};
  double a = abs(x), s = LIBC_NAMESPACE::sin(y), c = LIBC_NAMESPACE::cos(y);
  if (a < 0.5) {
    double sh = LIBC_NAMESPACE::sinh(x), ch = LIBC_NAMESPACE::cosh(x);
    // Avoid squaring a tiny denominator near a pole.
    double d = LIBC_NAMESPACE::hypot(sh, c);
    return {(sh / d) * (ch / d), (s / d) * (c / d)};
  }
  // Never evaluate sin(2*y): doubling a finite y could overflow.
  double e = a > 350.0 ? 0.0 : LIBC_NAMESPACE::exp(-2.0 * a);
  double m = 1.0 - e;
  double d = m * m + 4.0 * e * c * c;
  // The factor four must be included BEFORE the final subnormal rounding.
  // exp(-2*a) alone may round to zero while the imaginary part is nonzero.
  double im = a > 400.0 ? LIBC_NAMESPACE::copysign(0.0, s * c)
                        : exp_component(-2.0 * a, (s * c) / d, 2);
  return {LIBC_NAMESPACE::copysign(m * (1.0 + e) / d, x), im};
}

LIBC_INLINE Pair sin(Pair z) {
  Pair w = sinh({-z.imag, z.real});
  return {w.imag, -w.real};
}

LIBC_INLINE Pair cos(Pair z) { return cosh({-z.imag, z.real}); }

LIBC_INLINE Pair tan(Pair z) {
  Pair w = tanh({-z.imag, z.real});
  return {w.imag, -w.real};
}

} // namespace complex_detail
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_COMPLEX_TRANSCENDENTAL_H
