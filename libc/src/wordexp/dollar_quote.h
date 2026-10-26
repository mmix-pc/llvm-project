//===-- Dollar-single-quote decoding ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_DOLLAR_QUOTE_H
#define LLVM_LIBC_SRC_WORDEXP_DOLLAR_QUOTE_H

#include "src/wordexp/syntax.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct QuotedUnit {
  enum class Kind { CHARACTER, BYTE };
  Kind kind;
  uint32_t value;
  // Offsets in the decoded quote body; literal units can recover their exact
  // source spelling without confusing an escape's spelling with its value.
  size_t begin = 0, end = 0;
  bool literal = false;
};

struct QuotedSink {
  void *context = nullptr;
  // Encode CHARACTER using the selected locale, reporting an encoding error
  // instead of substituting bytes. BYTE is an explicitly requested raw byte.
  int (*emit)(void *, QuotedUnit) = nullptr;
};

// Input is decoded and excludes $' and the closing quote. Validate the entire
// span before invoking the sink. Numeric NUL discards the rest of this quoted
// span, not the surrounding word; discarded escapes are still syntax checked.
// Unspecified unknown/overlarge escapes are rejected. No shell is invoked.
// A failing sink owns its partial output and determines rollback policy.
WordSyntaxResult decode_dollar_quote(cpp::span<const uint32_t> input,
                                     QuotedSink sink = {});

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
