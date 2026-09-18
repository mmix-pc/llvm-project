//===-- BSD-backed POSIX regexec -----------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/regex/regexec.h"
#include "src/__support/CPP/new.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/regex/bsd_regex.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, regexec,
                   (const regex_t *__restrict preg,
                    const char *__restrict string, size_t nmatch,
                    regmatch_t *__restrict pmatch, int eflags)) {
  const auto *compiled = static_cast<const BSDRegex *>(preg->__internal);
  if (!compiled || (eflags & ~(REG_NOTBOL | REG_NOTEOL)))
    return REG_BADPAT;
  if (compiled->nosub)
    nmatch = 0;
  size_t count = nmatch < preg->re_nsub + 1 ? nmatch : preg->re_nsub + 1;
  llvm_regmatch_t *matches = nullptr;
  if (count && !compiled->empty) {
    AllocChecker ac;
    matches = new (ac) llvm_regmatch_t[count];
    if (!ac)
      return REG_ESPACE;
  }
  int error = compiled->empty
                  ? 0
                  : __llvm_libc_bsd_regexec(&compiled->engine, string, count,
                                            matches, eflags);
  if (!error) {
    for (size_t i = 0; i < nmatch; ++i) {
      if (i >= count)
        pmatch[i] = {-1, -1};
      else if (compiled->empty)
        pmatch[i] = {0, 0};
      else
        pmatch[i] = {matches[i].rm_so, matches[i].rm_eo};
    }
  }
  delete[] matches;
  return error <= REG_BADRPT ? error : REG_BADPAT;
}
} // namespace LIBC_NAMESPACE_DECL
