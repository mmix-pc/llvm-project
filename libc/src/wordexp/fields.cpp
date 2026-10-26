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

int FieldSplitter::finish() {
  if (count == cpp::numeric_limits<size_t>::max())
    return EOVERFLOW;
  if (int error = sink.finish(sink.context))
    return error;
  ++count;
  candidate = false;
  return 0;
}

int FieldSplitter::append(cpp::string_view bytes, bool quoted) {
  if (whitespace && candidate)
    if (int error = finish())
      return error;
  whitespace = false;
  if (int error = sink.append(sink.context, bytes, quoted))
    return error;
  candidate = true;
  return 0;
}

int FieldSplitter::delimiter(bool space) {
  if (space) {
    whitespace = true;
  } else {
    if (int error = finish())
      return error;
    whitespace = false;
  }
  return 0;
}

int FieldSplitter::control(cpp::string_view bytes, bool quoted) {
  // Shift-only suffixes preserve an existing field, but cannot create a new
  // field after a delimiter or turn leading whitespace into an empty field.
  return candidate && !whitespace ? sink.append(sink.context, bytes, quoted)
                                  : 0;
}

ErrorOr<size_t> FieldSplitter::complete() {
  if (candidate)
    if (int error = finish())
      return Error(error);
  return count;
}

ErrorOr<size_t> split_word_fields(cpp::span<const ExpansionPart> parts,
                                  cpp::span<const FieldDelimiter> delimiters,
                                  FieldSink sink) {
  if (!sink.append || !sink.finish)
    return Error(EINVAL);
  for (const auto &delimiter : delimiters)
    if (delimiter.bytes.empty())
      return Error(EINVAL);

  FieldSplitter fields(sink);

  for (const auto &part : parts) {
    if (!part.split || part.quoted) {
      if (!part.bytes.empty() || part.quoted)
        if (int error = fields.append(part.bytes, part.quoted))
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
        if (int error = fields.append(part.bytes.substr(at, 1), false))
          return Error(error);
        ++at;
      } else {
        at += matched->bytes.size();
        if (int error = fields.delimiter(matched->whitespace))
          return Error(error);
      }
    }
  }
  return fields.complete();
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
