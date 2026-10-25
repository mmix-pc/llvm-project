//===-- Validated collation records -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_COLLATION_DATA_H
#define LLVM_LIBC_SRC_LOCALE_COLLATION_DATA_H

#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// A borrowed immutable table. Unlisted scalars follow all explicit weights,
// share a primary weight and have codepoint-ordered subsequent weights.
class CollationData {
public:
  static constexpr size_t MAX_RULES = 131072;
  static constexpr size_t MAX_SEQUENCE = 256;
  static constexpr uint32_t MAX_WEIGHT = 262144;
  static constexpr size_t MAX_WORDS = 8 * 1024 * 1024;
  struct Weights {
    LocaleDataView::Item words;
    uint32_t single = 0;
    size_t size() const { return single ? 1 : words.bytes.size() / 4; }
    ErrorOr<uint32_t> at(size_t index) const;
  };
  struct Rule {
    LocaleDataView::Item words;
    uint32_t fallback_scalar = 0;
    uint32_t fallback_base = 0;
    uint32_t fallback_directions = 0;
    size_t source_size() const {
      return fallback_base ? 1 : words.word(0).value();
    }
    uint32_t source(size_t index) const {
      return fallback_base ? fallback_scalar : words.word(1 + index).value();
    }
    // 1 = forward, 2 = backward; bit 4 adds position sensitivity.
    ErrorOr<uint32_t> direction(size_t level) const;
    ErrorOr<Weights> weights(size_t level) const;
  };

  static ErrorOr<CollationData> read(const LocaleDataView &file);
  size_t size() const { return count; }
  ErrorOr<Rule> rule(size_t index) const;
  ErrorOr<Rule> find(cpp::span<const uint32_t> source) const;
  ErrorOr<Rule> longest(cpp::span<const uint32_t> source) const;
  // Resolve one longest element, including the implicit UNDEFINED rule.
  ErrorOr<Rule> lookup(cpp::span<const uint32_t> source) const;

private:
  LocaleDataView file;
  size_t count;
  size_t longest_source = 1;
  uint32_t fallback_base;
  uint32_t fallback_directions;
  CollationData(LocaleDataView file, size_t count, uint32_t fallback_base,
                uint32_t fallback_directions)
      : file(file), count(count), fallback_base(fallback_base),
        fallback_directions(fallback_directions) {}
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_COLLATION_DATA_H
