//===-- BSD-backed POSIX regfree -----------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/regex/regfree.h"
#include "src/__support/CPP/new.h"
#include "src/__support/common.h"
#include "src/regex/compiled_regex.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, regfree, (regex_t * preg)) {
  delete static_cast<internal::CompiledRegex *>(preg->__internal);
  preg->__internal = nullptr;
  preg->re_nsub = 0;
}
} // namespace LIBC_NAMESPACE_DECL
