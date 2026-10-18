//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wctype/wctrans.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(wctrans_t, wctrans, (const char *property)) {
  static const int lower = 1;
  static const int upper = 2;
  cpp::string_view name(property);
  if (name == "tolower")
    return reinterpret_cast<wctrans_t>(&lower);
  if (name == "toupper")
    return reinterpret_cast<wctrans_t>(&upper);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
