//===-- Character boundaries in narrow format strings -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_WCHAR_FORMAT_STRING_H
#define LLVM_LIBC_SRC___SUPPORT_WCHAR_FORMAT_STRING_H

#include "src/__support/wchar/restartable_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class FormatStringReader {
  RestartableConverter converter = RestartableConverter::current();
  mbstate state{};

  // Preserve the original bytes, including complete shift-only suffixes.
  // Invalid format encodings are outside the format contract; consume the
  // remainder as literal text rather than interpreting bytes as arguments.
  bool next(const char *text, size_t &position, uint32_t &scalar) {
    while (text[position]) {
      uint8_t byte = static_cast<uint8_t>(text[position++]);
      auto result = converter.decode(&scalar, {&byte, 1}, state);
      if (!result) {
        while (text[position])
          ++position;
        return false;
      }
      if (*result != size_t(-2))
        return true;
    }
    return false;
  }

public:
  void find_percent(const char *text, size_t &position) {
    if (!converter.state_dependent()) {
      // UTF-8 continuation bytes cannot contain an ASCII percent sign.
      while (text[position] && text[position] != '%')
        ++position;
      return;
    }
    uint32_t scalar;
    while (next(text, position, scalar)) {
      if (scalar == '%' && text[position - 1] == '%') {
        --position;
        return;
      }
    }
  }

  template <typename CharT>
  void find_percent(const CharT *text, size_t &position) {
    while (text[position] && text[position] != '%')
      ++position;
  }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
