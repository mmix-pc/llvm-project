//===-- Parameter pattern removal ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_PATTERN_H
#define LLVM_LIBC_SRC_WORDEXP_PATTERN_H

#include "src/__support/char_vector.h"
#include "src/fnmatch/matcher.h"
#include "src/wordexp/parameter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// The expanded pattern already represents quoted metacharacters with escapes.
// Decode the value once; trial matches and the remaining value each start in
// the initial encoding state. In particular, a suffix is not a raw substring
// starting inside an ISO-2022 shift sequence. Append to output only after all
// matching succeeds; allocation failure can leave a partial appended result.
// Success returns the number of characters removed, not the number of bytes.
ErrorOr<size_t>
remove_word_pattern(cpp::string_view pattern, cpp::string_view value,
                    ParameterExpansion::Operation operation,
                    RestartableConverter converter, const __locale_data *ctype,
                    const CollationData *collation, CharVector &output);

// Character count for ${#name}; shift sequences are not characters.
ErrorOr<size_t> word_parameter_length(cpp::string_view value,
                                      RestartableConverter converter);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
