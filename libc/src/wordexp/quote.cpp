//===-- Encoded dollar-single-quoted words --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/quote.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
struct Renderer {
  RestartableConverter converter;
  CharVector &output;
  WordQuoteState &encoding;
  const WordSource *source = nullptr;
  size_t begin = 0;

  int append(cpp::string_view bytes) {
    for (char byte : bytes)
      if (!output.append(byte))
        return ENOMEM;
    return 0;
  }
  int raw(cpp::string_view bytes) {
    if (int error = append(bytes))
      return error;
    if (converter.state_dependent() && encoding.valid) {
      while (!bytes.empty()) {
        uint32_t scalar;
        auto length = converter.decode(
            &scalar,
            {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()},
            encoding.state);
        if (!length || !*length) {
          encoding.valid = false;
          break;
        }
        if (*length == size_t(-2))
          break;
        bytes.remove_prefix(*length);
      }
    }
    return 0;
  }
  int transition(const mbstate &required) {
    uint8_t bytes[MAX_LOCALE_STATE_PREFIX_BYTES];
    auto length = converter.transition(required, encoding.state, bytes);
    return length ? append({reinterpret_cast<const char *>(bytes), *length})
                  : length.error();
  }
  int unit(QuotedUnit unit) {
    encoding.character_tail = unit.kind == QuotedUnit::Kind::CHARACTER;
    if (unit.kind == QuotedUnit::Kind::BYTE) {
      const char byte = char(unit.value);
      return raw({&byte, 1});
    }
    if (!encoding.valid || !converter.boundary(encoding.state))
      return EILSEQ;
    if (source && unit.literal) {
      if (int error = transition(source->units()[begin + unit.begin].before))
        return error;
      auto bytes = source->slice(begin + unit.begin, begin + unit.end);
      return bytes ? raw(*bytes) : bytes.error();
    }
    uint8_t bytes[MAX_LOCALE_CHARACTER_BYTES];
    auto length = converter.encode(unit.value, bytes, encoding.state);
    return length ? append({reinterpret_cast<const char *>(bytes), *length})
                  : length.error();
  }
  static int emit(void *p, QuotedUnit unit) {
    return static_cast<Renderer *>(p)->unit(unit);
  }
  WordSyntaxResult run(cpp::span<const uint32_t> body,
                       bool finish_character = true) {
    if (output.length())
      return {WordSyntaxError::SINK_ERROR, 0, EINVAL};
    auto result = decode_dollar_quote(body, {this, emit});
    if (!result)
      return result;
    // A complete stateful character result needs a reset before later word
    // fragments. Never invent a repair for intentionally raw invalid bytes.
    if (finish_character && encoding.character_tail && encoding.valid &&
        converter.boundary(encoding.state))
      if (int error = transition({}))
        return {WordSyntaxError::SINK_ERROR, body.size(), error};
    return result;
  }
};
} // namespace

WordSyntaxResult render_word_quote(cpp::span<const uint32_t> body,
                                   RestartableConverter converter,
                                   CharVector &output, WordQuoteState *state) {
  WordQuoteState local;
  return Renderer{converter, output, state ? *state : local}.run(body);
}

WordSyntaxResult render_word_quote(const WordSource &source, size_t begin,
                                   size_t end, RestartableConverter converter,
                                   CharVector &output, WordQuoteState *state,
                                   bool finish_character) {
  if (begin > end || end > source.scalars().size())
    return {WordSyntaxError::SINK_ERROR, begin, EINVAL};
  WordQuoteState local;
  return Renderer{converter, output, state ? *state : local, &source, begin}
      .run(source.scalars().subspan(begin, end - begin), finish_character);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
