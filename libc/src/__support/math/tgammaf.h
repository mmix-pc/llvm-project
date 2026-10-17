//===-- Implementation header for tgammaf -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMAF_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMAF_H

#include "src/__support/math/tgamma.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE float tgammaf(float x) { return gamma_double_internal::gamma(x); }

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMAF_H
