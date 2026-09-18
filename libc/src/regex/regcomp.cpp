//===-- BSD-backed POSIX regcomp -----------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/regex/regcomp.h"
#include "src/__support/CPP/new.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/regex/bsd_regex.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, regcomp,
                   (regex_t *__restrict preg, const char *__restrict pattern,
                    int cflags)) {
  preg->re_nsub = 0;
  preg->__internal = nullptr;
  if (cflags & ~(REG_EXTENDED | REG_ICASE | REG_NOSUB | REG_NEWLINE))
    return REG_BADPAT;
  AllocChecker ac;
  auto *compiled = new (ac) BSDRegex;
  if (!ac)
    return REG_ESPACE;
  compiled->empty = *pattern == '\0';
  compiled->nosub = cflags & REG_NOSUB;
  // LLVM Regex rejects empty patterns; POSIX libc consumers need empty matches.
  if (!compiled->empty) {
    int error = __llvm_libc_bsd_regcomp(&compiled->engine, pattern, cflags);
    if (error) {
      delete compiled;
      return error <= REG_BADRPT ? error : REG_BADPAT;
    }
  }
  preg->re_nsub = compiled->engine.re_nsub;
  preg->__internal = compiled;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
