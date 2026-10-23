//===-- Float type specifier converter for scanf ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_SCANF_CORE_FLOAT_CONVERTER_H
#define LLVM_LIBC_SRC_STDIO_SCANF_CORE_FLOAT_CONVERTER_H

#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/char_vector.h"
#include "src/__support/ctype_utils.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/scanf_core/converter_utils.h"
#include "src/stdio/scanf_core/reader.h"

namespace LIBC_NAMESPACE_DECL {
namespace scanf_core {

// Both scanners consume the longest initial subsequence, not strtod's shorter
// accepted prefix. Peek never reads beyond the field width, and the first byte
// or wide character after the item remains available to the caller.
template <typename Peek, typename Take>
int scan_float_token(Peek source_peek, Take source_take, size_t width,
                     const FormatSection &section) {
  CharVector token;
  bool allocation_failed = false;
  auto peek = [&]() -> int { return width ? source_peek() : -1; };
  auto take = [&]() {
    if (!token.append(static_cast<char>(source_peek()))) {
      allocation_failed = true;
      libc_errno = ENOMEM;
      width = 0;
      return;
    }
    source_take();
    --width;
  };
  auto lower = [](int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
  auto digit = [](int c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  if (peek() < 0)
    return FILE_READ_ERROR;
  if (peek() == '+' || peek() == '-')
    take();
  bool valid = false;
  if (lower(peek()) == 'i') {
    size_t n = 0;
    constexpr char word[] = "infinity";
    while (n < 8 && lower(peek()) == word[n]) {
      take();
      ++n;
    }
    valid = n == 3 || n == 8;
  } else if (lower(peek()) == 'n') {
    size_t n = 0;
    constexpr char word[] = "nan";
    while (n < 3 && lower(peek()) == word[n]) {
      take();
      ++n;
    }
    valid = n == 3;
    if (valid && peek() == '(') {
      take();
      while ((peek() >= '0' && peek() <= '9') ||
             (lower(peek()) >= 'a' && lower(peek()) <= 'z') || peek() == '_')
        take();
      valid = peek() == ')';
      if (valid)
        take();
    }
  } else {
    int base = 10;
    bool digits = false;
    if (peek() == '0') {
      take();
      digits = true;
      if (lower(peek()) == 'x') {
        take();
        base = 16;
        digits = false;
      }
    }
    auto read_digits = [&](int radix) {
      bool any = false;
      while (digit(peek()) >= 0 && digit(peek()) < radix) {
        any = true;
        take();
      }
      return any;
    };
    digits = read_digits(base) || digits;
    if (peek() == '.') {
      take();
      digits = read_digits(base) || digits;
    }
    valid = digits;
    if (digits && lower(peek()) == (base == 16 ? 'p' : 'e')) {
      take();
      if (peek() == '+' || peek() == '-')
        take();
      valid = read_digits(10);
    }
  }
  if (allocation_failed)
    return ALLOCATION_FAILURE;
  if (!valid)
    return MATCHING_FAILURE;
  write_float_with_length(token.c_str(), section);
  return READ_OK;
}

template <typename T>
int convert_float(Reader<T> *reader, const FormatSection &to_conv) {
  struct Lookahead {
    Reader<T> *reader;
    int next = -1;
    bool cached = false;
    ~Lookahead() {
      if (cached)
        reader->ungetc(next);
    }
    int peek() {
      if (!cached) {
        next = reader->getc();
        cached = true;
      }
      return next;
    }
    void take() { cached = false; }
  } input{reader};
  size_t width = to_conv.max_width > 0 ? size_t(to_conv.max_width)
                                       : cpp::numeric_limits<size_t>::max();
  return scan_float_token([&] { return input.peek(); }, [&] { input.take(); },
                          width, to_conv);
}

} // namespace scanf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_SCANF_CORE_FLOAT_CONVERTER_H
