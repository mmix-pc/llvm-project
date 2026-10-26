//===-- BSD-backed POSIX regexec -----------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/regex/regexec.h"
#include "src/__support/common.h"
#include "src/regex/compiled_regex.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, regexec,
                   (const regex_t *__restrict preg,
                    const char *__restrict string, size_t nmatch,
                    regmatch_t *__restrict pmatch, int eflags)) {
  const auto *compiled =
      static_cast<const internal::CompiledRegex *>(preg->__internal);
  if (!compiled || (eflags & ~(REG_NOTBOL | REG_NOTEOL)))
    return REG_BADPAT;
  int error = compiled->execute(string, nmatch, pmatch, eflags);
  return error <= REG_BADRPT ? error : REG_BADPAT;
}
} // namespace LIBC_NAMESPACE_DECL
