//===-- Validated transliteration records -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_TRANSLITERATION_DATA_H
#define LLVM_LIBC_SRC_LOCALE_TRANSLITERATION_DATA_H

#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Views borrow immutable storage from a retained LocaleDataFile. Alternatives
// keep their source order; an empty alternative is a real deletion rule.
class TransliterationData {
public:
  static constexpr size_t MAX_RULES = 65536;
  static constexpr size_t MAX_SEQUENCE = 256;
  static constexpr size_t MAX_ALTERNATIVES = 256;
  static constexpr size_t MAX_WORDS = 4 * 1024 * 1024;
  struct Rule {
    LocaleDataView::Item words;
    size_t source_size() const { return words.word(0).value(); }
    uint32_t source(size_t index) const {
      return words.word(1 + index).value();
    }
    size_t alternatives() const {
      return words.word(1 + source_size()).value();
    }
    ErrorOr<LocaleDataView::Item> alternative(size_t index) const;
  };

  static ErrorOr<TransliterationData> read(const LocaleDataView &file);
  size_t size() const { return count; }
  ErrorOr<Rule> rule(size_t index) const;
  // Exact multi-scalar lookup. Stream lookahead and longest-match selection
  // belong to the conversion policy, not this immutable data reader.
  ErrorOr<Rule> find(cpp::span<const uint32_t> source) const;
  // First lexicographic rule with this prefix. A complete shorter key sorts
  // before its extensions, giving deterministic incremental rule selection.
  ErrorOr<Rule> prefix(cpp::span<const uint32_t> source) const;

private:
  LocaleDataView file;
  size_t count;
  TransliterationData(LocaleDataView file, size_t count)
      : file(file), count(count) {}
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_TRANSLITERATION_DATA_H
