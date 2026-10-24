//===-- Transactional character conversion ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_WCHAR_ENCODING_CONVERTER_H
#define LLVM_LIBC_SRC___SUPPORT_WCHAR_ENCODING_CONVERTER_H

#include "src/__support/CPP/string_view.h"
#include "src/__support/wchar/iso2022_cn.h"
#include "src/__support/wchar/scalar_codec.h"
#include "src/locale/transliteration_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

enum class TextEncoding {
  ASCII,
  LATIN1,
  UTF8,
  UTF16LE,
  UTF16BE,
  UTF32LE,
  UTF32BE,
  ISO2022CN
};

// Allocation and immutable mapping ownership belong to the descriptor layer.
// A mapping supplied here must outlive the converter and every copy of it.
class EncodingConverter {
public:
  enum class NonIdentical { REPLACE, DISCARD, IGNORE, TRANSLIT };
  static ErrorOr<EncodingConverter>
  create(TextEncoding from, TextEncoding to,
         NonIdentical policy = NonIdentical::REPLACE,
         const ChineseEncodingData *mapping = nullptr);

  // Canonical names and explicit aliases only, with one destination indicator.
  // Empty names must be resolved by the public locale-aware descriptor layer.
  static ErrorOr<EncodingConverter>
  from_names(cpp::string_view from, cpp::string_view to,
             const ChineseEncodingData *mapping = nullptr);

  // Completed characters/shifts advance the spans even on a later error.
  // An incomplete or unconvertible character leaves its entire input suffix
  // and destination state untouched. Counts apply only to this invocation.
  ErrorOr<size_t> convert(cpp::span<const uint8_t> &input,
                          cpp::span<uint8_t> &output,
                          const TransliterationData *rules = nullptr);

  // A null output resets both states without emitting bytes. Otherwise reset
  // is atomic: E2BIG preserves both states and the output span.
  ErrorOr<size_t> reset(cpp::span<uint8_t> *output);

private:
  TextEncoding from, to;
  NonIdentical policy;
  const ChineseEncodingData *mapping;
  ISO2022CNState source, destination;

  EncodingConverter(TextEncoding from, TextEncoding to, NonIdentical policy,
                    const ChineseEncodingData *mapping)
      : from(from), to(to), policy(policy), mapping(mapping) {}
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_WCHAR_ENCODING_CONVERTER_H
