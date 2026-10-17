//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/complex/cacos.h"
#include "src/__support/common.h"
#include "src/__support/complex_inverse.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(_Complex double, cacos, (_Complex double z)) {
  return complex_detail::pack<double>(
      complex_detail::acos(complex_detail::unpack<double>(z)));
}

} // namespace LIBC_NAMESPACE_DECL
