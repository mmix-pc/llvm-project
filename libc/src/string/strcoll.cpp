//===-- Implementation of strcoll -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/string/strcoll.h"
#include "src/locale/collation.h"

#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/macros/null_check.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, strcoll, (const char *left, const char *right)) {
  LIBC_CRASH_ON_NULLPTR(left);
  LIBC_CRASH_ON_NULLPTR(right);
  return internal::collate(left, right, internal::current_collation());
}

} // namespace LIBC_NAMESPACE_DECL
