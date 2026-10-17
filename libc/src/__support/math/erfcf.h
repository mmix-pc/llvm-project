//===-- Implementation header for erfcf -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_ERFCF_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_ERFCF_H

#include "erfc.h"
#include "hdr/errno_macros.h"
#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/FPUtil/except_value_utils.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE float erfcf(float x) {
  using Bits = fputil::FPBits<float>;
  Bits bits(x);
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  if (bits.is_inf())
    return bits.is_neg() ? 2.0f : 0.0f;
  if (bits.is_zero())
    return 1.0f;
  // Preserve directed rounding where evaluating in double could lose the
  // correction before narrowing to float.
  if (bits.abs().get_val() < 0x1p-26f)
    return bits.is_neg() ? fputil::round_result_slightly_up(1.0f)
                         : fputil::round_result_slightly_down(1.0f);
  if (x <= -4.0f)
    return fputil::round_result_slightly_down(2.0f);
  if (x >= 11.0f)
    return erfc_internal::underflow<float>();

  // All intermediates in this interval are normal binary64 numbers. Only
  // the conversion can underflow, and its rounded value determines errno.
  float result = static_cast<float>(math::erfc(static_cast<double>(x)));
  if (Bits(result).uintval() < Bits::min_normal().uintval()) {
    fputil::set_errno_if_required(ERANGE);
    fputil::raise_except_if_required(FE_UNDERFLOW | FE_INEXACT);
  }
  return result;
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_ERFCF_H
