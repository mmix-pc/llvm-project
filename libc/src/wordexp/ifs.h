//===-- Owned word expansion delimiters --------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_IFS_H
#define LLVM_LIBC_SRC_WORDEXP_IFS_H

#include "src/__support/wchar/restartable_converter.h"
#include "src/wordexp/fields.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class WordDelimiters {
  char *bytes = nullptr;
  FieldDelimiter *storage = nullptr;
  size_t count = 0;
  bool default_value = true;

public:
  WordDelimiters() = default;
  WordDelimiters(const WordDelimiters &) = delete;
  WordDelimiters &operator=(const WordDelimiters &) = delete;
  ~WordDelimiters();

  // Own the selected IFS bytes; failure preserves the previous selection.
  // Unset and empty are distinct. Delimiters retain their original encoding,
  // including shift prefixes, except space/tab/newline are always single-byte
  // delimiters. A complete shift-only suffix is not itself a character.
  // Input excludes NUL and remains stable throughout both decoding passes.
  int assign(bool set, cpp::string_view value, RestartableConverter converter);
  cpp::span<const FieldDelimiter> delimiters() const {
    if (default_value)
      return DEFAULT_FIELD_DELIMITERS;
    return {storage, count};
  }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
