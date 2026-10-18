//===-- UTF-8 conversion helpers for uchar--------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UCHAR_CONVERSION_H
#define LLVM_LIBC_SRC_UCHAR_CONVERSION_H
#include "hdr/errno_macros.h"
#include "hdr/types/size_t.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/wchar/character_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
// Values outside the UTF-8 length range keep pending UTF-16 units non-initial
// for mbsinit without changing the public mbstate_t layout.
inline constexpr uint8_t PENDING_HIGH = 5;
inline constexpr uint8_t PENDING_LOW = 6;

LIBC_INLINE size_t decode(char32_t *out, const char *s, size_t n,
                          mbstate *state) {
  if (!s) {
    out = nullptr;
    s = "";
    n = 1;
  }
  LocaleCharacterConverter converter(state);
  if (!converter.isValidState()) {
    libc_errno = EILSEQ;
    return size_t(-1);
  }
  for (size_t i = 0; i < n; ++i) {
    if (converter.push(static_cast<char8_t>(s[i])) != 0) {
      libc_errno = EILSEQ;
      return size_t(-1);
    }
    if (converter.isFull()) {
      char32_t scalar = converter.pop_utf32().value();
      if (out)
        *out = scalar;
      return scalar == 0 ? 0 : i + 1;
    }
  }
  return size_t(-2);
}

LIBC_INLINE size_t encode(char *s, char32_t scalar, mbstate *state) {
  char buffer[4];
  if (!s) {
    s = buffer;
    scalar = 0;
  }
  LocaleCharacterConverter converter(state);
  if (!converter.isValidState() || !converter.isEmpty() ||
      converter.push(scalar) != 0) {
    libc_errno = EILSEQ;
    return size_t(-1);
  }
  size_t count = 0;
  while (!converter.isEmpty())
    s[count++] = static_cast<char>(converter.pop_utf8().value());
  return count;
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
