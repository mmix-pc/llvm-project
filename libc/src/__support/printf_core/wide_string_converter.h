//===-- Character and string conversion for wide printf ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_STRING_CONVERTER_H
#define LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_STRING_CONVERTER_H

#include "hdr/types/wint_t.h"
#include "src/__support/printf_core/converter_utils.h"
#include "src/__support/printf_core/wide_writer.h"
#include "src/__support/wchar/string_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace printf_core {

LIBC_INLINE ErrorOr<wchar_t>
next_wide_character(internal::StringConverter<char8_t> &converter) {
  size_t start = converter.getSourceIndex();
  auto value = converter.pop<char32_t>();
  if (!value.has_value())
    return Error(MB_CONVERSION_ERROR);
  size_t bytes = converter.getSourceIndex() - start;
  char32_t scalar = value.value();
  // The shared decoder assembles bytes but does not validate Unicode scalars
  // or shortest-form encoding. Formatting must report these as encoding errors.
  if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) ||
      (bytes == 2 && scalar < 0x80) || (bytes == 3 && scalar < 0x800) ||
      (bytes == 4 && scalar < 0x10000))
    return Error(MB_CONVERSION_ERROR);
  return static_cast<wchar_t>(scalar);
}

LIBC_INLINE size_t wide_padding(const BasicFormatSection<wchar_t> &section,
                                size_t length) {
  return section.min_width > 0 && size_t(section.min_width) > length
             ? size_t(section.min_width) - length
             : 0;
}

LIBC_INLINE int convert_wide_char(WideWriter *writer,
                                  const BasicFormatSection<wchar_t> &section) {
  wchar_t value;
  if (section.length_modifier == LengthModifier::l) {
    value = static_cast<wchar_t>(static_cast<wint_t>(section.conv_val_raw));
  } else {
    // As with btowc, a single byte must form a complete character by itself.
    char8_t byte = static_cast<unsigned char>(section.conv_val_raw);
    internal::mbstate state;
    internal::StringConverter<char8_t> converter(&byte, &state, 1, 1);
    auto converted = converter.pop<char32_t>();
    if (!converted.has_value())
      return MB_CONVERSION_ERROR;
    value = static_cast<wchar_t>(converted.value());
  }
  size_t padding = wide_padding(section, 1);
  bool left = (section.flags & FormatFlags::LEFT_JUSTIFIED) != 0;
  if (!left)
    RET_IF_RESULT_NEGATIVE(writer->write(L' ', padding));
  RET_IF_RESULT_NEGATIVE(writer->write(value));
  if (left)
    RET_IF_RESULT_NEGATIVE(writer->write(L' ', padding));
  return WRITE_OK;
}

LIBC_INLINE int
convert_wide_string(WideWriter *writer,
                    const BasicFormatSection<wchar_t> &section) {
  const bool wide = section.length_modifier == LengthModifier::l;
  const auto *wide_text = static_cast<const wchar_t *>(section.conv_val_ptr);
  const auto *bytes = static_cast<const char8_t *>(section.conv_val_ptr);
  const size_t limit =
      section.precision < 0 ? SIZE_MAX : size_t(section.precision);
  size_t length = 0;
  internal::mbstate state;
  internal::StringConverter<char8_t> counter(bytes, &state, limit);
  // Count only the selected prefix; neither pass examines input past precision.
  while (length < limit) {
    wchar_t value;
    if (wide) {
      value = wide_text[length];
    } else {
      auto converted = next_wide_character(counter);
      if (!converted.has_value())
        return MB_CONVERSION_ERROR;
      value = static_cast<wchar_t>(converted.value());
    }
    if (value == L'\0')
      break;
    if (length == size_t(cpp::numeric_limits<int>::max()))
      return OVERFLOW_ERROR;
    ++length;
  }

  size_t padding = wide_padding(section, length);
  bool left = (section.flags & FormatFlags::LEFT_JUSTIFIED) != 0;
  if (!left)
    RET_IF_RESULT_NEGATIVE(writer->write(L' ', padding));
  if (wide) {
    RET_IF_RESULT_NEGATIVE(writer->write({wide_text, length}));
  } else {
    state = internal::mbstate();
    internal::StringConverter<char8_t> converter(bytes, &state, length);
    for (size_t i = 0; i < length; ++i) {
      auto converted = next_wide_character(converter);
      if (!converted.has_value())
        return MB_CONVERSION_ERROR;
      RET_IF_RESULT_NEGATIVE(
          writer->write(static_cast<wchar_t>(converted.value())));
    }
  }
  if (left)
    RET_IF_RESULT_NEGATIVE(writer->write(L' ', padding));
  return WRITE_OK;
}

} // namespace printf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_STRING_CONVERTER_H
