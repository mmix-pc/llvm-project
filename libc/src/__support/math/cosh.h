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

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_COSH_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_COSH_H

#include "exp.h"
#include "expm1.h"
#include "hyperbolic_utils.h"
#include "src/__support/FPUtil/except_value_utils.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE double cosh(double x) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  if (bits.is_inf_or_nan())
    return x * x;
  double ax = bits.abs().get_val();
  if (bits.is_zero())
    return 1.0;
  if (ax < 0x1p-27)
    return fputil::round_result_slightly_up(1.0);
  // cosh(x) = 1 + expm1(x)^2 / (2 * exp(x)) near zero.
  if (ax < 0x1.62e43p-2) {
    double t = math::expm1(ax);
    double w = 1.0 + t;
    return 1.0 + t * t / (w + w);
  }
  if (ax < 22.0) {
    double t = math::exp(ax);
    return 0.5 * t + 0.5 / t;
  }
  if (ax < 0x1.62e42p+9)
    return 0.5 * math::exp(ax);
  if (ax <= 0x1.633ce8fb9f87dp+9)
    return hyperbolic_internal::exp_half(ax, false);
  return hyperbolic_internal::overflow(false);
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif
