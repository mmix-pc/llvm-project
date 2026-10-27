//===-- BSD-backed POSIX regcomp -----------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/regex/regcomp.h"
#include "src/__support/CPP/new.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/regex/compiled_regex.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, regcomp,
                   (regex_t *__restrict preg, const char *__restrict pattern,
                    int cflags)) {
  preg->re_nsub = 0;
  preg->__internal = nullptr;
  if (cflags & ~(REG_EXTENDED | REG_ICASE | REG_NOSUB | REG_NEWLINE | REG_MINIMAL))
    return REG_BADPAT;
  AllocChecker ac;
  auto *compiled = new (ac) internal::CompiledRegex;
  if (!ac)
    return REG_ESPACE;
  if (int error = compiled->compile(pattern, cflags)) {
    delete compiled;
    return error <= REG_BADRPT ? error : REG_BADPAT;
  }
  preg->re_nsub = compiled->subexpressions();
  preg->__internal = compiled;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
