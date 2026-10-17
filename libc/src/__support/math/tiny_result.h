//===-- Tiny result rounding ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_TINY_RESULT_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_TINY_RESULT_H

#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

// The exact result differs from x by a nonzero correction smaller than half
// the spacing to either neighbour. Avoid underflow in that correction, and
// detect tininess after rounding. Callers handle non-finite inputs separately.
template <typename T> LIBC_INLINE T round_tiny_result(T x, bool upward) {
  using Bits = fputil::FPBits<T>;
  // An exact operation preserves the platform's FTZ/DAZ behavior.
  volatile T one = T(1);
  T input = x * one;
  Bits bits(input);
  if (bits.is_zero())
    return input;
#ifndef LIBC_MATH_HAS_ASSUME_ROUND_NEAREST_ONLY
  int mode = fputil::get_round();
  if ((upward && mode == FE_UPWARD) || (!upward && mode == FE_DOWNWARD) ||
      (mode == FE_TOWARDZERO && upward == bits.is_neg()))
    bits = Bits(bits.uintval() + (upward != bits.is_neg() ? 1 : -1));
#endif
  fputil::raise_except_if_required(
      FE_INEXACT | (bits.get_biased_exponent() == 0 ? FE_UNDERFLOW : 0));
  return bits.get_val() * one;
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_TINY_RESULT_H
