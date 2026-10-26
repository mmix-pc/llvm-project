//===-- Origin-aware word expansion field splitting -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/fields.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<size_t> split_word_fields(cpp::span<const ExpansionPart> parts,
                                  cpp::span<const FieldDelimiter> delimiters,
                                  FieldSink sink) {
  if (!sink.append || !sink.finish)
    return Error(EINVAL);
  for (const auto &delimiter : delimiters)
    if (delimiter.bytes.empty())
      return Error(EINVAL);

  size_t count = 0;
  bool candidate = false, whitespace = false;
  auto finish = [&]() -> int {
    if (count == cpp::numeric_limits<size_t>::max())
      return EOVERFLOW;
    if (int error = sink.finish(sink.context))
      return error;
    ++count;
    candidate = false;
    return 0;
  };
  auto append = [&](cpp::string_view bytes, bool quoted) -> int {
    if (whitespace && candidate)
      if (int error = finish())
        return error;
    whitespace = false;
    if (int error = sink.append(sink.context, bytes, quoted))
      return error;
    candidate = true;
    return 0;
  };

  for (const auto &part : parts) {
    if (!part.split || part.quoted) {
      if (!part.bytes.empty() || part.quoted)
        if (int error = append(part.bytes, part.quoted))
          return Error(error);
      continue;
    }
    size_t at = 0;
    while (at != part.bytes.size()) {
      const FieldDelimiter *matched = nullptr;
      for (const auto &delimiter : delimiters)
        if (part.bytes.substr(at, delimiter.bytes.size()) == delimiter.bytes) {
          matched = &delimiter;
          break;
        }
      if (!matched) {
        if (int error = append(part.bytes.substr(at, 1), false))
          return Error(error);
        ++at;
      } else {
        at += matched->bytes.size();
        if (matched->whitespace) {
          // IFS whitespace and the following non-whitespace delimiter together
          // terminate one field, including across adjacent expansion results.
          whitespace = true;
        } else {
          if (int error = finish())
            return Error(error);
          whitespace = false;
        }
      }
    }
  }
  if (candidate)
    if (int error = finish())
      return Error(error);
  return count;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
