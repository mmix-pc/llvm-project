//===-- Character-oriented BSD regex composition ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_REGEX_CHARACTER_REGEX_H
#define LLVM_LIBC_SRC_REGEX_CHARACTER_REGEX_H

#include "hdr/types/regmatch_t.h"
#include "src/regex/bsd_regex.h"
#include "src/regex/decoded_text.h"

struct __locale_data;

namespace LIBC_NAMESPACE_DECL {
namespace internal {
class CollationData;

// Private composition, not yet selected by public regcomp/regexec. Minimal
// matching and public category ownership remain separate integration work.
// Callers retain category/converter data and use the same locale for both calls.
class CharacterRegex {
  llvm_regex_t engine{};
  bool ready = false, empty = false, nosub = false;

public:
  CharacterRegex() = default;
  CharacterRegex(const CharacterRegex &) = delete;
  CharacterRegex &operator=(const CharacterRegex &) = delete;
  ~CharacterRegex();

  // A failed compilation preserves the previous expression.
  int compile(cpp::string_view pattern, int flags,
              RestartableConverter converter,
              const __locale_data *ctype = nullptr,
              const CollationData *collation = nullptr);
  int execute(cpp::string_view input, size_t count, regmatch_t *matches,
              int flags, RestartableConverter converter) const;
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
