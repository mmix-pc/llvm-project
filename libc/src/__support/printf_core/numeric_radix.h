//===-- Locale radix output for numeric converters --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_NUMERIC_RADIX_H
#define LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_NUMERIC_RADIX_H

#include "src/__support/CPP/type_traits.h"
#include "src/__support/printf_core/wide_writer.h"
#include "src/locale/numeric_locale.h"

namespace LIBC_NAMESPACE_DECL {
namespace printf_core {

// The digit algorithms count a radix as one logical position. Padding and
// output counts instead use bytes for printf and wide characters for wprintf.
template <typename WriterT> class NumericRadix {
  internal::NumericParseLocale locale =
      internal::current_numeric_parse_locale();

public:
  LIBC_INLINE explicit NumericRadix(
      internal::NumericParseLocale selected =
          internal::current_numeric_parse_locale())
      : locale(selected) {}
  LIBC_INLINE size_t size() const {
    if constexpr (cpp::is_same_v<WriterT, WideNumericWriter>)
      return 1;
    else
      return locale.radix.size();
  }
  LIBC_INLINE int write(WriterT *writer) const {
    if constexpr (cpp::is_same_v<WriterT, WideNumericWriter>)
      return writer->write_wide(locale.wide_radix);
    else
      return writer->write(locale.radix);
  }
};

template <typename WriterT> class NumericGrouping {
  internal::NumericParseLocale locale =
      internal::current_numeric_parse_locale();
  bool enabled;

public:
  LIBC_INLINE explicit NumericGrouping(
      bool enabled, internal::NumericParseLocale selected =
                        internal::current_numeric_parse_locale())
      : locale(selected), enabled(enabled) {}
  LIBC_INLINE bool active() const {
    return enabled && !locale.thousands.empty() && !locale.grouping.empty();
  }
  // remaining is the number of digits to the right of a possible separator.
  LIBC_INLINE bool boundary(size_t remaining) const {
    if (!enabled || locale.thousands.empty() || !remaining)
      return false;
    size_t consumed = 0, previous = 0;
    for (uint8_t group : locale.grouping) {
      if (group == 127)
        return false;
      if (group == 0)
        return previous && remaining > consumed &&
               (remaining - consumed) % previous == 0;
      consumed += group;
      previous = group;
      if (remaining <= consumed)
        return remaining == consumed;
    }
    return false;
  }
  LIBC_INLINE size_t separator_size() const {
    if constexpr (cpp::is_same_v<WriterT, WideNumericWriter>)
      return 1;
    else
      return locale.thousands.size();
  }
  LIBC_INLINE size_t separator_count(size_t digits) const {
    if (!active() || !digits)
      return 0;
    size_t result = 0, remaining = digits - 1, previous = 0;
    for (uint8_t group : locale.grouping) {
      if (group == 127)
        break;
      if (group == 0) {
        if (previous)
          result += remaining / previous;
        break;
      }
      if (group > remaining)
        break;
      ++result;
      remaining -= group;
      previous = group;
    }
    return result;
  }
  LIBC_INLINE size_t extra_size(size_t digits) const {
    return separator_count(digits) * separator_size();
  }
  LIBC_INLINE int write_separator(WriterT *writer) const {
    if constexpr (cpp::is_same_v<WriterT, WideNumericWriter>)
      return writer->write_wide(locale.wide_thousands);
    else
      return writer->write(locale.thousands);
  }
  LIBC_INLINE int write(WriterT *writer, cpp::string_view digits) const {
    if (!active())
      return writer->write(digits);
    size_t start = 0;
    for (size_t i = 1; i < digits.size(); ++i) {
      if (!boundary(digits.size() - i))
        continue;
      int result = writer->write(digits.substr(start, i - start));
      if (result < 0)
        return result;
      result = write_separator(writer);
      if (result < 0)
        return result;
      start = i;
    }
    return writer->write(digits.substr(start));
  }
};

} // namespace printf_core
} // namespace LIBC_NAMESPACE_DECL
#endif
