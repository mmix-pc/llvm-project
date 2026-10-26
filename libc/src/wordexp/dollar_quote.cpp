//===-- Dollar-single-quote decoding --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/dollar_quote.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
int digit(uint32_t c, unsigned base) {
  if (c >= '0' && c <= '9')
    return c - '0' < base ? int(c - '0') : -1;
  if (base == 16 && c >= 'a' && c <= 'f')
    return int(c - 'a' + 10);
  if (base == 16 && c >= 'A' && c <= 'F')
    return int(c - 'A' + 10);
  return -1;
}

WordSyntaxResult decode(cpp::span<const uint32_t> input, QuotedSink sink) {
  bool discard = false;
  for (size_t at = 0; at < input.size();) {
    size_t start = at;
    auto invalid = [start]() {
      return WordSyntaxResult{WordSyntaxError::SYNTAX, start};
    };
    QuotedUnit unit{QuotedUnit::Kind::CHARACTER, input[at++]};
    if (!unit.value || unit.value > 0x10ffff ||
        (unit.value >= 0xd800 && unit.value <= 0xdfff) || unit.value == '\'')
      return invalid();
    if (unit.value == '\\') {
      if (at == input.size())
        return invalid();
      uint32_t c = input[at++];
      if (c == 'x' || digit(c, 8) >= 0) {
        unsigned base = c == 'x' ? 16 : 8;
        unsigned limit = base == 16 ? 2 : 3, count = 0;
        unit = {QuotedUnit::Kind::BYTE, 0};
        if (base == 8)
          --at;
        while (at < input.size() && digit(input[at], base) >= 0 &&
               count < limit) {
          unit.value = unit.value * base + uint32_t(digit(input[at++], base));
          ++count;
        }
        if (!count || unit.value > 255 ||
            (base == 16 && at < input.size() && digit(input[at], 16) >= 0))
          return invalid();
        if (!unit.value)
          discard = true;
      } else if (c == 'c') {
        if (at == input.size())
          return invalid();
        c = input[at++];
        if (c >= 'a' && c <= 'z')
          unit.value = c - 'a' + 1;
        else if (c >= 'A' && c <= 'Z')
          unit.value = c - 'A' + 1;
        else if (c >= '[' && c <= '_') {
          if (c == '\\' && (at == input.size() || input[at++] != '\\'))
            return invalid();
          unit.value = c - '[' + 27;
        } else if (c == '?')
          unit.value = 127;
        else
          return invalid();
      } else {
        switch (c) {
        case '\"':
        case '\'':
        case '\\':
          unit.value = c;
          break;
        case 'a':
          unit.value = '\a';
          break;
        case 'b':
          unit.value = '\b';
          break;
        case 'e':
          unit.value = 27;
          break;
        case 'f':
          unit.value = '\f';
          break;
        case 'n':
          unit.value = '\n';
          break;
        case 'r':
          unit.value = '\r';
          break;
        case 't':
          unit.value = '\t';
          break;
        case 'v':
          unit.value = '\v';
          break;
        default:
          return invalid();
        }
      }
    }
    if (!discard && sink.emit)
      if (int error = sink.emit(sink.context, unit))
        return {WordSyntaxError::SINK_ERROR, start, error};
  }
  return {WordSyntaxError::NONE, input.size()};
}
} // namespace

WordSyntaxResult decode_dollar_quote(cpp::span<const uint32_t> input,
                                     QuotedSink sink) {
  auto checked = decode(input, {});
  return checked ? decode(input, sink) : checked;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
