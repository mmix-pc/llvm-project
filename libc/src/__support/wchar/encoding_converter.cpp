//===-- Transactional character conversion --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/wchar/encoding_converter.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
bool same_name(cpp::string_view a, cpp::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i != a.size(); ++i) {
    char value = a[i];
    if (value >= 'a' && value <= 'z')
      value = char(value - 'a' + 'A');
    if (value != b[i])
      return false;
  }
  return true;
}

ErrorOr<TextEncoding> named_encoding(cpp::string_view name) {
  struct Name {
    cpp::string_view text;
    TextEncoding encoding;
  };
  constexpr Name names[] = {{"ASCII", TextEncoding::ASCII},
                            {"US-ASCII", TextEncoding::ASCII},
                            {"ISO-8859-1", TextEncoding::LATIN1},
                            {"UTF-8", TextEncoding::UTF8},
                            {"UTF-16LE", TextEncoding::UTF16LE},
                            {"UTF-16BE", TextEncoding::UTF16BE},
                            {"UTF-32LE", TextEncoding::UTF32LE},
                            {"UTF-32BE", TextEncoding::UTF32BE},
                            {"ISO-2022-CN", TextEncoding::ISO2022CN}};
  for (const auto &candidate : names)
    if (same_name(name, candidate.text))
      return candidate.encoding;
  return Error(EINVAL);
}

ErrorOr<ScalarEncoding> scalar_encoding(TextEncoding encoding) {
  switch (encoding) {
  case TextEncoding::ASCII:
    return ScalarEncoding::ASCII;
  case TextEncoding::LATIN1:
    return ScalarEncoding::LATIN1;
  case TextEncoding::UTF8:
    return ScalarEncoding::UTF8;
  case TextEncoding::UTF16LE:
    return ScalarEncoding::UTF16LE;
  case TextEncoding::UTF16BE:
    return ScalarEncoding::UTF16BE;
  case TextEncoding::UTF32LE:
    return ScalarEncoding::UTF32LE;
  case TextEncoding::UTF32BE:
    return ScalarEncoding::UTF32BE;
  case TextEncoding::ISO2022CN:
    break;
  }
  return Error(EINVAL);
}

struct InputStep {
  uint32_t scalar;
  size_t bytes;
  bool shift;
};

size_t input_unit(TextEncoding encoding) {
  if (encoding == TextEncoding::UTF16LE || encoding == TextEncoding::UTF16BE)
    return 2;
  if (encoding == TextEncoding::UTF32LE || encoding == TextEncoding::UTF32BE)
    return 4;
  return 1;
}

ErrorOr<InputStep> decode(TextEncoding from, ISO2022CNState &state,
                          const ChineseEncodingData *mapping,
                          cpp::span<const uint8_t> input) {
  if (from != TextEncoding::ISO2022CN) {
    auto encoding = scalar_encoding(from);
    if (!encoding)
      return Error(encoding.error());
    auto decoded = decode_scalar(*encoding, input);
    if (!decoded)
      return Error(decoded.error());
    return InputStep{decoded->value, decoded->bytes, false};
  }
  for (size_t i = 0; i != input.size(); ++i) {
    auto step = state.push(input[i], *mapping);
    if (!step)
      return Error(step.error());
    if (step->event != ISO2022CNState::Event::INCOMPLETE)
      return InputStep{step->scalar, i + 1,
                       step->event == ISO2022CNState::Event::SHIFT};
  }
  return Error(EINVAL);
}

ErrorOr<size_t> encode(TextEncoding to, ISO2022CNState &state,
                       const ChineseEncodingData *mapping, uint32_t scalar,
                       cpp::span<uint8_t> output) {
  if (to == TextEncoding::ISO2022CN)
    return state.encode(scalar, output, *mapping);
  auto encoding = scalar_encoding(to);
  if (!encoding)
    return Error(encoding.error());
  return encode_scalar(*encoding, scalar, output);
}

struct Transliterated {
  size_t consumed, characters, written;
  ISO2022CNState source, destination;
};

ErrorOr<Transliterated> transliterate(const TransliterationData &rules,
                                      TextEncoding from, TextEncoding to,
                                      const ChineseEncodingData *mapping,
                                      cpp::span<const uint8_t> input,
                                      InputStep first, ISO2022CNState source,
                                      ISO2022CNState destination,
                                      cpp::span<uint8_t> buffer) {
  uint32_t key[TransliterationData::MAX_SEQUENCE];
  key[0] = first.scalar;
  size_t count = 1, consumed = first.bytes;
  auto rule = rules.prefix({key, count});
  while (rule && rule->source_size() > count) {
    if (consumed == input.size())
      return Error(EINVAL);
    auto next = decode(from, source, mapping, input.subspan(consumed));
    if (!next) {
      if (next.error() == EINVAL)
        return Error(EINVAL);
      // An invalid following character cannot participate in this rule. The
      // caller can still replace the original character, then report it.
      return Error(ENOENT);
    }
    consumed += next->bytes;
    if (next->shift)
      continue;
    if (next->scalar == 0)
      return Error(ENOENT);
    key[count++] = next->scalar;
    rule = rules.prefix({key, count});
  }
  if (!rule)
    return Error(rule.error());
  for (size_t i = 0; i != rule->alternatives(); ++i) {
    ISO2022CNState candidate = destination;
    size_t written = 0;
    auto alternative = rule->alternative(i).value();
    bool representable = true;
    for (size_t j = 0; j != alternative.bytes.size() / 4; ++j) {
      auto result = encode(to, candidate, mapping, alternative.word(j).value(),
                           buffer.subspan(written));
      if (!result) {
        if (result.error() != EILSEQ)
          return Error(result.error());
        representable = false;
        break;
      }
      written += *result;
    }
    if (representable)
      return Transliterated{consumed, count, written, source, candidate};
  }
  return Error(ENOENT);
}
} // namespace

ErrorOr<EncodingConverter>
EncodingConverter::from_names(cpp::string_view from, cpp::string_view to,
                              const ChineseEncodingData *mapping) {
  auto source = named_encoding(from);
  if (!source)
    return Error(source.error());
  NonIdentical selected = NonIdentical::REPLACE;
  size_t suffix = to.find_first_of('/');
  if (suffix != cpp::string_view::npos) {
    auto indicator = to.substr(suffix);
    if (same_name(indicator, "//NON_IDENTICAL_DISCARD"))
      selected = NonIdentical::DISCARD;
    else if (same_name(indicator, "//IGNORE"))
      selected = NonIdentical::IGNORE;
    else if (same_name(indicator, "//TRANSLIT"))
      selected = NonIdentical::TRANSLIT;
    else
      return Error(EINVAL);
    to = to.substr(0, suffix);
  }
  auto destination = named_encoding(to);
  if (!destination)
    return Error(destination.error());
  return create(*source, *destination, selected, mapping);
}

ErrorOr<EncodingConverter>
EncodingConverter::create(TextEncoding from, TextEncoding to,
                          NonIdentical policy,
                          const ChineseEncodingData *mapping) {
  const TextEncoding encodings[] = {from, to};
  for (auto encoding : encodings) {
    if (encoding == TextEncoding::ISO2022CN) {
      if (!mapping)
        return Error(EINVAL);
    } else if (!scalar_encoding(encoding)) {
      return Error(EINVAL);
    }
  }
  if (policy != NonIdentical::REPLACE && policy != NonIdentical::DISCARD &&
      policy != NonIdentical::IGNORE && policy != NonIdentical::TRANSLIT)
    return Error(EINVAL);
  return EncodingConverter(from, to, policy, mapping);
}

ErrorOr<size_t> EncodingConverter::convert(cpp::span<const uint8_t> &input,
                                           cpp::span<uint8_t> &output,
                                           const TransliterationData *rules) {
  if (policy == NonIdentical::TRANSLIT && !rules)
    return Error(EINVAL);
  size_t nonidentical = 0;
  while (!input.empty()) {
    ISO2022CNState next_source = source;
    auto step = decode(from, next_source, mapping, input);
    if (!step) {
      if (step.error() != EILSEQ || policy != NonIdentical::IGNORE)
        return Error(step.error());
      // Probe without modifying the committed state. Discard only when a
      // complete valid successor exists at a source-code-unit boundary.
      size_t unit = input_unit(from);
      size_t skipped = 0;
      while (input.size() - skipped > unit) {
        skipped += unit;
        next_source = source;
        auto candidate =
            decode(from, next_source, mapping, input.subspan(skipped));
        if (candidate) {
          input = input.subspan(skipped);
          step = candidate;
          break;
        }
      }
      if (!step)
        return Error(EILSEQ);
    }
    if (step->shift) {
      source = next_source;
      input = input.subspan(step->bytes);
      continue;
    }

    // Stage one conversion, including its complete bounded transliteration,
    // before publishing any bytes, source progress or destination shift state.
    ISO2022CNState next_destination = destination;
    uint8_t bytes[8 * TransliterationData::MAX_SEQUENCE];
    auto size = encode(to, next_destination, mapping, step->scalar, bytes);
    size_t different = 0;
    if (!size) {
      if (size.error() != EILSEQ)
        return Error(size.error());
      different = 1;
      if (policy == NonIdentical::DISCARD || policy == NonIdentical::IGNORE) {
        size = size_t(0);
      } else {
        if (policy == NonIdentical::TRANSLIT) {
          auto result = transliterate(*rules, from, to, mapping, input, *step,
                                      next_source, destination, bytes);
          if (result) {
            step->bytes = result->consumed;
            different = result->characters;
            next_source = result->source;
            next_destination = result->destination;
            size = result->written;
          } else if (result.error() != ENOENT) {
            return Error(result.error());
          }
        }
        if (!size) {
          size = encode(to, next_destination, mapping, '?', bytes);
          if (!size)
            return Error(size.error());
        }
      }
    }
    if (*size > output.size())
      return Error(E2BIG);
    for (size_t i = 0; i != *size; ++i)
      output[i] = bytes[i];
    // Avoid null-pointer arithmetic when a discarded scalar emits no bytes.
    if (*size)
      output = output.subspan(*size);
    input = input.subspan(step->bytes);
    source = next_source;
    destination = next_destination;
    nonidentical += different;
  }
  return nonidentical;
}

ErrorOr<size_t> EncodingConverter::reset(cpp::span<uint8_t> *output) {
  if (output && to == TextEncoding::ISO2022CN) {
    auto size = destination.reset(*output);
    if (!size)
      return Error(size.error());
    if (*size)
      *output = output->subspan(*size);
  }
  source = {};
  destination = {};
  return size_t(0);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
