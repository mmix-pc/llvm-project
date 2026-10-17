//===-- Implementation header for atanh -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Adapted from fdlibm e_atanh.c:
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_ATANH_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_ATANH_H

#include "hdr/errno_macros.h"
#include "hyperbolic_utils.h"
#include "log1p.h"
#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE double atanh(double x) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  double ax = bits.abs().get_val();
  if (ax > 1.0) {
    fputil::set_errno_if_required(EDOM);
    fputil::raise_except_if_required(FE_INVALID);
    return Bits::quiet_nan().get_val();
  }
  if (ax == 1.0) {
    fputil::set_errno_if_required(ERANGE);
    fputil::raise_except_if_required(FE_DIVBYZERO);
    return Bits::inf(bits.sign()).get_val();
  }
  if (ax < 0x1p-28)
    return hyperbolic_internal::tiny_odd(x, true);
  double t = 2.0 * ax;
  double result =
      0.5 * math::log1p(ax < 0.5 ? t + t * ax / (1.0 - ax) : t / (1.0 - ax));
  return bits.is_neg() ? -result : result;
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_ATANH_H
