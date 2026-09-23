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

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_TANH_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_TANH_H

#include "expm1.h"
#include "hyperbolic_utils.h"
#include "src/__support/FPUtil/except_value_utils.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE double tanh(double x) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  if (bits.is_nan())
    return x + x;
  if (bits.is_inf())
    return bits.is_neg() ? -1.0 : 1.0;
  double ax = bits.abs().get_val();
  if (ax < 0x1p-28)
    return hyperbolic_internal::tiny_odd(x, false);
  if (ax >= 22.0)
    return bits.is_neg() ? fputil::round_result_slightly_up(-1.0)
                         : fputil::round_result_slightly_down(1.0);
  double z;
  if (ax < 1.0) {
    double t = math::expm1(-2.0 * ax);
    z = -t / (t + 2.0);
  } else {
    double t = math::expm1(2.0 * ax);
    z = 1.0 - 2.0 / (t + 2.0);
  }
  return bits.is_neg() ? -z : z;
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif
