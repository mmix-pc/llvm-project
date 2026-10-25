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
  char buffer[internal::MAX_LOCALE_CHARACTER_BYTES];
  if (s == nullptr) {
    s = buffer;
    c = 0;
  }
  using Converter = internal::RestartableConverter;
  using Unit = Converter::UTF16Unit;
  auto converter = Converter::current();
  if (converter.pending(*state, Unit::HIGH)) {
    auto high = converter.take_unit(*state, Unit::HIGH);
    if (!high || c < 0xdc00 || c > 0xdfff) {
      libc_errno = EILSEQ;
      return size_t(-1);
    }
    return internal::encode(s, 0x10000 + ((*high - 0xd800) << 10) + (c - 0xdc00),
                            state);
  }
  if (c >= 0xd800 && c <= 0xdbff) {
    if (int error = converter.save_unit(*state, Unit::HIGH, c)) {
      libc_errno = error;
      return size_t(-1);
    }
    return 0;
  }
  return internal::encode(s, c, state);
}
} // namespace LIBC_NAMESPACE_DECL
