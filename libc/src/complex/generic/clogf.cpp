//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/complex/clogf.h"
#include "src/__support/common.h"
#include "src/__support/complex_transcendental.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(_Complex float, clogf, (_Complex float z)) {
  return complex_detail::pack<float>(
      complex_detail::log(complex_detail::unpack<float>(z)));
}

} // namespace LIBC_NAMESPACE_DECL
