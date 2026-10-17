//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/complex/cpow.h"
#include "src/__support/common.h"
#include "src/__support/complex_power.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(_Complex double, cpow,
                   (_Complex double z, _Complex double w)) {
  return complex_detail::pack<double>(complex_detail::pow(
      complex_detail::unpack<double>(z), complex_detail::unpack<double>(w)));
}

} // namespace LIBC_NAMESPACE_DECL
