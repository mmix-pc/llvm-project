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

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_SINH_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_SINH_H

#include "exp.h"
#include "expm1.h"
#include "hyperbolic_utils.h"
#include "src/__support/FPUtil/except_value_utils.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE double sinh(double x) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  if (bits.is_inf_or_nan())
    return x + x;
  double ax = bits.abs().get_val();
  if (ax < 0x1p-28)
    return hyperbolic_internal::tiny_odd(x, true);
  double half = bits.is_neg() ? -0.5 : 0.5;
  // expm1 avoids cancellation near zero.
  if (ax < 22.0) {
    double t = math::expm1(ax);
    if (ax < 1.0)
      return half * (2.0 * t - t * t / (t + 1.0));
    return half * (t + t / (t + 1.0));
  }
  if (ax < 0x1.62e42p+9)
    return half * math::exp(ax);
  if (ax <= 0x1.633ce8fb9f87dp+9)
    return hyperbolic_internal::exp_half(ax, bits.is_neg());
  return hyperbolic_internal::overflow(bits.is_neg());
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif
