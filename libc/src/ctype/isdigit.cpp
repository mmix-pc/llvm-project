//===-- Implementation of isdigit ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ctype/isdigit.h"
#include "src/__support/common.h"
#include "src/locale/character.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, isdigit, (int c)) {
  return internal::classify_byte(c, internal::WCTYPE_DIGIT,
                                 internal::current_ctype());
}

} // namespace LIBC_NAMESPACE_DECL
