//===-- Decoded regular expression text -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_REGEX_DECODED_TEXT_H
#define LLVM_LIBC_SRC_REGEX_DECODED_TEXT_H

#include "hdr/types/regoff_t.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/wchar/restartable_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class DecodedRegexText {
public:
  struct Unit {
    uint32_t scalar;
    regoff_t begin, end;
  };

  DecodedRegexText() = default;
  DecodedRegexText(const DecodedRegexText &) = delete;
  DecodedRegexText &operator=(const DecodedRegexText &) = delete;
  ~DecodedRegexText();

  // Input excludes the C terminator and remains immutable during both passes.
  // Failure preserves the previous decoded text. Each unit includes any shift
  // prefix consumed before its character; a complete shift-only suffix remains
  // separately represented by [characters_end(), bytes()). No re-encoding is
  // used to reconstruct positions, and no capture-offset policy is implied.
  int assign(cpp::string_view input, RestartableConverter converter);
  cpp::span<const Unit> units() const { return {storage, count}; }
  regoff_t bytes() const { return byte_count; }
  regoff_t characters_end() const { return character_end; }

private:
  Unit *storage = nullptr;
  size_t count = 0;
  regoff_t byte_count = 0, character_end = 0;
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
