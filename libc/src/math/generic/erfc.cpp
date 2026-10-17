//===-- Implementation of erfc ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/erfc.h"
#include "src/__support/math/erfc.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(double, erfc, (double x)) { return math::erfc(x); }

} // namespace LIBC_NAMESPACE_DECL
