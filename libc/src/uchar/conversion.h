//===-- Locale conversion helpers for uchar ------------------------------===//
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
#include "src/__support/wchar/restartable_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
LIBC_INLINE size_t decode(char32_t *out, const char *s, size_t n,
                          mbstate *state) {
  if (!s) {
    out = nullptr;
    s = "";
    n = 1;
  }
  uint32_t scalar = 0;
  auto result = RestartableConverter::current().decode(
      &scalar, {reinterpret_cast<const uint8_t *>(s), n}, *state);
  if (!result) {
    libc_errno = result.error();
    return size_t(-1);
  }
  if (out && *result != size_t(-2))
    *out = char32_t(scalar);
  return *result;
}

LIBC_INLINE size_t encode(char *s, char32_t scalar, mbstate *state) {
  char buffer[MAX_LOCALE_CHARACTER_BYTES];
  if (!s) {
    s = buffer;
    scalar = 0;
  }
  auto result = RestartableConverter::current().encode(
      scalar, {reinterpret_cast<uint8_t *>(s), MAX_LOCALE_CHARACTER_BYTES},
      *state);
  if (!result) {
    libc_errno = result.error();
    return size_t(-1);
  }
  return *result;
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
