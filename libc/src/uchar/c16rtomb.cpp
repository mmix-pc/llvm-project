//===-- Implementation of c16rtomb----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/uchar/c16rtomb.h"
#include "src/uchar/conversion.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(size_t, c16rtomb,
                   (char *__restrict s, char16_t c, mbstate_t *__restrict ps)) {
  static LIBC_THREAD_LOCAL internal::mbstate implicit_state;
  auto *state =
      ps ? reinterpret_cast<internal::mbstate *>(ps) : &implicit_state;
  char buffer[4];
  if (s == nullptr) {
    s = buffer;
    c = 0;
  }
  if (state->total_bytes == internal::PENDING_HIGH) {
    char32_t high = state->partial;
    *state = {};
    if (c < 0xdc00 || c > 0xdfff) {
      libc_errno = EILSEQ;
      return size_t(-1);
    }
    return internal::encode(s, 0x10000 + ((high - 0xd800) << 10) + (c - 0xdc00),
                            state);
  }
  if (c >= 0xd800 && c <= 0xdbff) {
    internal::CharacterConverter converter(state);
    if (!converter.isValidState() || !converter.isEmpty()) {
      libc_errno = EILSEQ;
      return size_t(-1);
    }
    state->partial = c;
    state->bytes_stored = 1;
    state->total_bytes = internal::PENDING_HIGH;
    return 0;
  }
  return internal::encode(s, c, state);
}
} // namespace LIBC_NAMESPACE_DECL
