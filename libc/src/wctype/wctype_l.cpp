//===-- Implementation of wctype_l ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wctype/wctype_l.h"
#include "src/__support/common.h"
#include "src/__support/wctype_impl.h"

namespace LIBC_NAMESPACE_DECL {

// Standard class descriptors are stable IDs; their consumers select the tables.
LLVM_LIBC_FUNCTION(wctype_t, wctype_l, (const char *property, locale_t)) {
  return internal::wctype(property);
}

} // namespace LIBC_NAMESPACE_DECL
