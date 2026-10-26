//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcsftime.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/common.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/printf_core/writer.h"
#include "src/__support/wchar/character_converter.h"
#include "src/time/strftime_core/strftime_main.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
struct WideTimeBuffer {
  wchar_t *buffer;
  size_t capacity;
  size_t used = 0;
  internal::mbstate state{};

  int append(wchar_t c) {
    if (used == capacity)
      return -1;
    buffer[used++] = c;
    return 0;
  }

  static int write(cpp::string_view bytes, void *context) {
    auto &out = *static_cast<WideTimeBuffer *>(context);
    // LC_TIME records use normalized UTF-8 independently of LC_CTYPE.
    internal::CharacterConverter converter(&out.state);
    for (char byte : bytes) {
      int error = converter.push(static_cast<char8_t>(byte));
      if (error)
        return -error;
      if (converter.isFull()) {
        auto c = converter.pop<char32_t>();
        if (!c.has_value())
          return -c.error();
        if (out.append(static_cast<wchar_t>(c.value())) < 0)
          return -1;
      }
    }
    return 0;
  }
};
} // namespace

LLVM_LIBC_FUNCTION(size_t, wcsftime,
                   (wchar_t *__restrict buffer, size_t size,
                    const wchar_t *__restrict format,
                    const struct tm *timeptr)) {
  if (size > 0)
    LIBC_CRASH_ON_NULLPTR(buffer);
  LIBC_CRASH_ON_NULLPTR(format);
  LIBC_CRASH_ON_NULLPTR(timeptr);
  if (size == 0)
    return 0;
  (void)time_utils::refresh_timezone();
  const auto *locale = internal::current_time_locale();
  WideTimeBuffer output{buffer, size - 1};
  while (*format) {
    if (*format != L'%') {
      if (output.append(*format++) < 0)
        return 0;
      continue;
    }
    const wchar_t *start = format;
    ++format;
    strftime_core::FormatSection section;
    section.has_conv = true;
    while (*format == L'+' || *format == L'0') {
      section.flags = static_cast<strftime_core::FormatFlags>(
          section.flags | (*format == L'+' ? strftime_core::FORCE_SIGN
                                           : strftime_core::LEADING_ZEROES));
      ++format;
    }
    while (*format >= L'0' && *format <= L'9') {
      unsigned digit = unsigned(*format++ - L'0');
      if (unsigned(section.min_width) >
          (unsigned(cpp::numeric_limits<int>::max()) - digit) / 10)
        return 0;
      section.min_width = section.min_width * 10 + int(digit);
    }
    if (*format == L'E' || *format == L'O') {
      section.modifier = *format == L'E' ? strftime_core::ConvModifier::E
                                         : strftime_core::ConvModifier::O;
      ++format;
    }
    wchar_t conversion = *format;
    if (*format)
      ++format;
    if (conversion > 0 && conversion <= 0x7f &&
        cpp::string_view("aAbBcCdDeFgGhHIjmMnprRsStTuUVwWxXyYzZ%")
                .find_first_of(char(conversion)) != cpp::string_view::npos) {
      section.conv_name = char(conversion);
    } else {
      while (start != format)
        if (output.append(*start++) < 0)
          return 0;
      continue;
    }

    printf_core::FlushingBuffer sink(nullptr, 0, WideTimeBuffer::write,
                                     &output);
    printf_core::Writer writer(sink);
    int result = strftime_core::convert_locale_time(&writer, section, timeptr,
                                                    locale, 0, true);
    if (result < 0 || output.state.bytes_stored != 0)
      return 0;
  }
  buffer[output.used] = L'\0';
  return output.used;
}
} // namespace LIBC_NAMESPACE_DECL
