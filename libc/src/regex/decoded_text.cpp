//===-- Decoded regular expression text -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/regex/decoded_text.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
struct Length {
  size_t count, character_end;
};

ErrorOr<Length> decode(cpp::string_view input, RestartableConverter converter,
                       DecodedRegexText::Unit *output) {
  mbstate state{};
  size_t at = 0, count = 0, end = 0;
  while (at < input.size()) {
    uint32_t scalar;
    auto read =
        converter.decode(&scalar,
                         {reinterpret_cast<const uint8_t *>(input.data() + at),
                          input.size() - at},
                         state);
    if (!read)
      return Error(read.error());
    if (*read == size_t(-2)) {
      if (!converter.boundary(state))
        return Error(EILSEQ);
      break;
    }
    if (!*read || *read > input.size() - at || !scalar)
      return Error(EILSEQ);
    if (output)
      output[count] = {scalar, regoff_t(at), regoff_t(at + *read)};
    ++count;
    at += *read;
    end = at;
  }
  return Length{count, end};
}
} // namespace

int DecodedRegexText::assign(cpp::string_view input,
                             RestartableConverter converter) {
  if (input.size() > size_t(cpp::numeric_limits<regoff_t>::max()))
    return EOVERFLOW;
  auto length = decode(input, converter, nullptr);
  if (!length)
    return length.error();
  if (length->count > cpp::numeric_limits<size_t>::max() / sizeof(Unit))
    return EOVERFLOW;
  Unit *next = nullptr;
  if (length->count) {
    AllocChecker ac;
    next = new (ac) Unit[length->count];
    if (!ac)
      return ENOMEM;
    auto decoded = decode(input, converter, next);
    if (!decoded) {
      ::free(next);
      return decoded.error();
    }
  }
  ::free(storage);
  storage = next;
  count = length->count;
  byte_count = regoff_t(input.size());
  character_end = regoff_t(length->character_end);
  return 0;
}

DecodedRegexText::~DecodedRegexText() { ::free(storage); }

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
