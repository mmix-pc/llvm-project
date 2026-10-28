//===-- Fixed-order Bessel error handling ------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_H

#include "bessel0.h"
#include "bessel1.h"
#include "hdr/errno_macros.h"
#include "hdr/fenv_macros.h"
#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/generic/div.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

template <unsigned Order, bool SecondKind> LIBC_INLINE double bessel(double x) {
  using Bits = fputil::FPBits<double>;
  Bits bits(x);
  if (bits.is_nan()) {
    if (bits.is_signaling_nan()) {
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
    return x;
  }
  if constexpr (SecondKind) {
    if (bits.is_zero()) {
      fputil::set_errno_if_required(ERANGE);
      fputil::raise_except_if_required(FE_DIVBYZERO);
      return Bits::inf(Sign::NEG).get_val();
    }
    if (bits.is_neg()) {
      fputil::set_errno_if_required(EDOM);
      fputil::raise_except_if_required(FE_INVALID);
      return Bits::quiet_nan().get_val();
    }
  }
  if (bits.is_inf())
    return Bits::zero(!SecondKind && Order == 1 ? bits.sign() : Sign::POS)
        .get_val();
  if (bits.is_zero())
    return Order == 0 ? 1.0 : x;
  if constexpr (SecondKind && Order == 1) {
    // Directed overflow can return a finite value. Use the range-aware
    // division for this asymptotic branch rather than inspect result infinity.
    if (bits.uintval() < uint64_t(0x3c90000000000000))
      return fputil::generic::div<double>(-bessel1_internal::tpi, x);
  }
  double result;
  if constexpr (Order == 0)
    result = SecondKind ? bessel0_internal::y0(x) : bessel0_internal::j0(x);
  else
    result = SecondKind ? bessel1_internal::y1(x) : bessel1_internal::j1(x);
  Bits result_bits(result);
  if (result_bits.is_inf()) {
    fputil::set_errno_if_required(ERANGE);
    fputil::raise_except_if_required(FE_OVERFLOW | FE_INEXACT);
  } else if (result_bits.is_subnormal() || result_bits.is_zero()) {
    fputil::set_errno_if_required(ERANGE);
    fputil::raise_except_if_required(FE_UNDERFLOW | FE_INEXACT);
  }
  return result;
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL
#endif
