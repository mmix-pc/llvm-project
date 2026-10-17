//===-- Implementation header for acosh -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Adapted from fdlibm e_acosh.c:
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_ACOSH_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_ACOSH_H

#include "hdr/errno_macros.h"
#include "log.h"
#include "log1p.h"
#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/FPUtil/double_double.h"
#include "src/__support/FPUtil/sqrt.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE double acosh(double x) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  if (x < 1.0) {
    fputil::set_errno_if_required(EDOM);
    fputil::raise_except_if_required(FE_INVALID);
    return Bits::quiet_nan().get_val();
  }
  if (bits.is_inf())
    return x;
  if (x == 1.0)
    return 0.0;
  if (x >= 0x1p28)
    return math::log(x) + 0x1.62e42fefa39efp-1;
  if (x > 2.0)
    return math::log(2.0 * x - 1.0 / (x + fputil::sqrt<double>(x * x - 1.0)));
  // Retain the product and root residual near x = 1; rounding the root
  // before log1p would amplify its error at the branch point.
  double t = x - 1.0;
  auto radicand = fputil::quick_mult(t, fputil::exact_add(2.0, t));
  double root = fputil::sqrt<double>(radicand.hi);
  auto square = fputil::exact_mult(root, root);
  double correction =
      ((radicand.hi - square.hi) + (radicand.lo - square.lo)) / (2.0 * root);
  auto sum = fputil::exact_add(root, t);
  return math::log1p(sum.hi) + (sum.lo + correction) / (1.0 + sum.hi);
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_ACOSH_H
