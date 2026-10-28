//===-- Implementation of j1 ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/j1.h"
#include "src/__support/common.h"
#include "src/__support/math/bessel.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(double, j1, (double x)) { return math::bessel<1, false>(x); }
} // namespace LIBC_NAMESPACE_DECL
