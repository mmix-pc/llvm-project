//===-- CPU implementation of tgammaf --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/tgammaf.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/math/tgammaf.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(float, tgammaf, (float x)) { return math::tgammaf(x); }

} // namespace LIBC_NAMESPACE_DECL
