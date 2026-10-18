//===-- Implementation of mbrtoc16----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/uchar/mbrtoc16.h"
#include "src/uchar/conversion.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(size_t, mbrtoc16,
                   (char16_t *__restrict out, const char *__restrict s,
                    size_t n, mbstate_t *__restrict ps)) {
  static internal::mbstate implicit_state;
  auto *state =
      ps ? reinterpret_cast<internal::mbstate *>(ps) : &implicit_state;
  if (s == nullptr) {
    out = nullptr;
    s = "";
    n = 1;
  }
  if (state->total_bytes == internal::PENDING_LOW) {
    if (out)
      *out = static_cast<char16_t>(state->partial);
    *state = {};
    return size_t(-3);
  }
  char32_t scalar;
  size_t count = internal::decode(&scalar, s, n, state);
  if (count == size_t(-1) || count == size_t(-2))
    return count;
  if (scalar >= 0x10000) {
    scalar -= 0x10000;
    if (out)
      *out = static_cast<char16_t>(0xd800 + (scalar >> 10));
    state->partial = 0xdc00 + (scalar & 0x3ff);
    state->bytes_stored = 1;
    state->total_bytes = internal::PENDING_LOW;
  } else if (out) {
    *out = static_cast<char16_t>(scalar);
  }
  return count;
}
} // namespace LIBC_NAMESPACE_DECL
