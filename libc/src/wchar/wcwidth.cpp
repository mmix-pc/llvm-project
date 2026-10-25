//===-- Implementation of wcwidth -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcwidth.h"
#include "src/__support/common.h"
#include "src/wchar/display_width.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, wcwidth, (wchar_t value)) {
  return internal::display_width(value, internal::current_ctype());
}
} // namespace LIBC_NAMESPACE_DECL
