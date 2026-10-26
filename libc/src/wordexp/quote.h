//===-- Encoded dollar-single-quoted words -----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_QUOTE_H
#define LLVM_LIBC_SRC_WORDEXP_QUOTE_H

#include "src/wordexp/dollar_quote.h"
#include "src/wordexp/source.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct WordQuoteState {
  mbstate state{};
  bool valid = true;
  bool character_tail = false;
};

// Output starts empty and retains any owned prefix on failure. Byte escapes
// are appended verbatim and update the output's decoder state. A following
// character cannot be encoded from an invalid or incomplete state. A byte-only
// result is not rejected merely because it is not valid encoded text.
// An optional state continues an existing word, including a partial numeric
// byte sequence. It retains progress on failure just as the output does.
WordSyntaxResult render_word_quote(cpp::span<const uint32_t> body,
                                   RestartableConverter converter,
                                   CharVector &output,
                                   WordQuoteState *state = nullptr);

// The converter matches source admission. Literal characters preserve their
// original byte spelling; only necessary state transitions are introduced.
// A byte-sensitive delimiter consumer can defer the automatic character-tail
// reset when it will append the original closing-quote control prefix itself.
WordSyntaxResult render_word_quote(const WordSource &source, size_t begin,
                                   size_t end, RestartableConverter converter,
                                   CharVector &output,
                                   WordQuoteState *state = nullptr,
                                   bool finish_character = true);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
