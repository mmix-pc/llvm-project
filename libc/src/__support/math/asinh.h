//===-- Implementation header for asinh -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Adapted from fdlibm s_asinh.c:
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_ASINH_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_ASINH_H

#include "hyperbolic_utils.h"
#include "log.h"
#include "log1p.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/FPUtil/sqrt.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE double asinh(double x) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  if (bits.is_inf())
    return x;
  double ax = bits.abs().get_val();
  if (ax < 0x1p-28)
    return hyperbolic_internal::tiny_odd(x, false);
  double result;
  if (ax >= 0x1p28) {
    result = math::log(ax) + 0x1.62e42fefa39efp-1;
  } else if (ax > 2.0) {
    result =
        math::log(2.0 * ax + 1.0 / (fputil::sqrt<double>(ax * ax + 1.0) + ax));
  } else {
    double t = ax * ax;
    result = math::log1p(ax + t / (1.0 + fputil::sqrt<double>(1.0 + t)));
  }
  return bits.is_neg() ? -result : result;
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_ASINH_H
