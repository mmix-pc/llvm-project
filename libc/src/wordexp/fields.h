//===-- Origin-aware word expansion field splitting -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_FIELDS_H
#define LLVM_LIBC_SRC_WORDEXP_FIELDS_H

#include "src/__support/CPP/span.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct ExpansionPart {
  cpp::string_view bytes;
  bool split;
  bool quoted;
};

struct FieldDelimiter {
  cpp::string_view bytes;
  bool whitespace;
};

inline constexpr FieldDelimiter DEFAULT_FIELD_DELIMITERS[] = {
    {" ", true}, {"\t", true}, {"\n", true}};

struct FieldSink {
  void *context;
  int (*append)(void *, cpp::string_view, bool quoted);
  int (*finish)(void *);
};

// Each part retains one expansion's origin; delimiter matching never joins
// bytes from separate parts. Empty quoted parts preserve an empty field.
// Delimiters have already been decoded from IFS by the caller; input bytes need
// not be valid characters. Sink errors stop processing immediately. The caller
// owns partial output and keeps all input storage stable throughout the call.
ErrorOr<size_t> split_word_fields(cpp::span<const ExpansionPart> parts,
                                  cpp::span<const FieldDelimiter> delimiters,
                                  FieldSink sink);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
