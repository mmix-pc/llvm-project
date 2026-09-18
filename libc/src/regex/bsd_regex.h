//===-- Private BSD regex adapter -----------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_SRC_REGEX_BSD_REGEX_H
#define LLVM_LIBC_SRC_REGEX_BSD_REGEX_H

#include "src/__support/macros/config.h"

// The application may itself link LLVM Support. Never export its engine names.
#define llvm_regcomp __llvm_libc_bsd_regcomp
#define llvm_regexec __llvm_libc_bsd_regexec
#define llvm_regfree __llvm_libc_bsd_regfree
#define llvm_strlcpy __llvm_libc_bsd_strlcpy
#include "regex_impl.h"

namespace LIBC_NAMESPACE_DECL {
struct BSDRegex {
  llvm_regex_t engine{};
  bool empty = false;
  bool nosub = false;
};
} // namespace LIBC_NAMESPACE_DECL
#endif
