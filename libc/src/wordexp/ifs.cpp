//===-- Owned word expansion delimiters -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/ifs.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"
#include "src/locale/character.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {

ErrorOr<size_t> decode(cpp::string_view value, RestartableConverter converter,
                       const __locale_data *ctype, FieldDelimiter *output) {
  mbstate state{};
  size_t count = 0, at = 0;
  while (at < value.size()) {
    uint32_t scalar;
    auto length =
        converter.decode(&scalar,
                         {reinterpret_cast<const uint8_t *>(value.data() + at),
                          value.size() - at},
                         state);
    if (!length)
      return Error(length.error());
    if (*length == size_t(-2)) {
      if (!converter.boundary(state))
        return Error(EILSEQ);
      break;
    }
    if (!*length || *length > value.size() - at || !scalar)
      return Error(EILSEQ);
    if (output) {
      auto bytes = value.substr(at, *length);
      bool portable = scalar == ' ' || scalar == '\t' || scalar == '\n';
      bool whitespace = portable ||
          (ctype ? classify_wide(scalar, WCTYPE_SPACE, ctype)
                 : scalar < 128 && isspace(static_cast<char>(scalar)));
      if (portable) {
        // POSIX requires these delimiters to be single bytes, even when their
        // decoded character consumed a preceding state-reset sequence.
        bytes = scalar == ' ' ? " " : scalar == '\t' ? "\t" : "\n";
      }
      output[count] = {bytes, whitespace, scalar};
    }
    ++count;
    at += *length;
  }
  return count;
}
} // namespace

int WordDelimiters::assign(bool set, cpp::string_view value,
                           RestartableConverter converter,
                           const __locale_data *ctype) {
  if (!set || value.empty()) {
    ::free(storage);
    ::free(bytes);
    storage = nullptr;
    bytes = nullptr;
    count = 0;
    default_value = !set;
    return 0;
  }
  auto decoded = decode(value, converter, ctype, nullptr);
  if (!decoded)
    return decoded.error();
  if (*decoded > cpp::numeric_limits<size_t>::max() / sizeof(FieldDelimiter))
    return EOVERFLOW;

  char *next_bytes = nullptr;
  FieldDelimiter *next = nullptr;
  if (*decoded) {
    next_bytes = static_cast<char *>(::malloc(value.size()));
    if (!next_bytes)
      return ENOMEM;
    inline_memcpy(next_bytes, value.data(), value.size());
    AllocChecker ac;
    next = new (ac) FieldDelimiter[*decoded];
    if (!ac) {
      ::free(next_bytes);
      return ENOMEM;
    }
    auto filled = decode({next_bytes, value.size()}, converter, ctype, next);
    if (!filled) {
      ::free(next);
      ::free(next_bytes);
      return filled.error();
    }
  }
  ::free(storage);
  ::free(bytes);
  storage = next;
  bytes = next_bytes;
  count = *decoded;
  default_value = false;
  return 0;
}

WordDelimiters::~WordDelimiters() {
  ::free(storage);
  ::free(bytes);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
