//===-- Implementation of lgammaf function --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/lgammaf.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/math/lgammaf.h"
#ifdef LIBC_MATH_HAS_SIGNGAM
#include "src/math/signgam.h"
#endif

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(float, lgammaf, (float x)) {
#ifdef LIBC_MATH_HAS_SIGNGAM
  using Bits = fputil::FPBits<float>;
  Bits bits(x);
  signgam = 1;
  if (bits.is_neg() && bits.abs().uintval() < Bits(0x1p23f).uintval()) {
    // Between negative integer poles Gamma alternates sign. Extract the
    // integer parity without introducing floating-point exceptions.
    unsigned exponent = bits.get_biased_exponent();
    unsigned parity = 0;
    if (exponent >= Bits::EXP_BIAS)
      parity = ((bits.get_mantissa() | (1U << Bits::FRACTION_LEN)) >>
                (Bits::FRACTION_LEN + Bits::EXP_BIAS - exponent)) &
               1;
    signgam = parity ? 1 : -1;
  }
#endif
  return math::lgammaf(x);
}

} // namespace LIBC_NAMESPACE_DECL
