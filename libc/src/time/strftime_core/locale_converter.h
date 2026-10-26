//===-- Calendar category expansions ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_TIME_STRFTIME_CORE_LOCALE_CONVERTER_H
#define LLVM_LIBC_SRC_TIME_STRFTIME_CORE_LOCALE_CONVERTER_H

#include "hdr/errno_macros.h"
#include "src/locale/time_locale.h"
#include "src/time/strftime_core/converter.h"
#include "src/time/strftime_core/parser.h"

namespace LIBC_NAMESPACE_DECL {
namespace strftime_core {
using TimeData = internal::TimeLocaleData;

LIBC_INLINE cpp::string_view locale_time_text(const TimeData &data,
                                              TimeData::Field field,
                                              size_t index = 0) {
  auto item = data.text(field, index);
  if (!item)
    return "?";
  auto bytes = item->bytes;
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size() - 1};
}

LIBC_INLINE int compare_era_dates(TimeData::Date a, TimeData::Date b) {
  if (a.infinity || b.infinity)
    return (a.infinity > b.infinity) - (a.infinity < b.infinity);
  if (a.year != b.year)
    return a.year < b.year ? -1 : 1;
  if (a.month != b.month)
    return a.month < b.month ? -1 : 1;
  return (a.day > b.day) - (a.day < b.day);
}

LIBC_INLINE cpp::optional<TimeData::Era> select_era(const TimeData &data,
                                                    const tm &time) {
  int64_t year = int64_t(time.tm_year) + 1900;
  TimeData::Date date{year <= 0 ? year - 1 : year, unsigned(time.tm_mon) + 1,
                      unsigned(time.tm_mday)};
  for (size_t i = 0; i != data.count(TimeData::ERAS); ++i) {
    auto era = data.era(i);
    if (!era)
      continue;
    int start = compare_era_dates(date, era->start);
    int end = compare_era_dates(date, era->end);
    if ((start >= 0 && end <= 0) || (start <= 0 && end >= 0))
      return *era;
  }
  return cpp::nullopt;
}

template <printf_core::WriteMode mode>
int format_time_sections(printf_core::Writer<mode> *writer, const char *format,
                         const tm *time, const TimeData *data,
                         unsigned depth = 0, bool wide_output = false);

template <printf_core::WriteMode mode>
int convert_locale_time(printf_core::Writer<mode> *writer,
                        const FormatSection &section, const tm *time,
                        const TimeData *data, unsigned depth,
                        bool wide_output = false) {
  if (!data)
    return convert(writer, section, time);
  cpp::optional<cpp::string_view> text;
  cpp::optional<cpp::string_view> format;
  switch (section.conv_name) {
  case 'a':
    text = locale_time_text(*data, TimeData::ABDAY, time->tm_wday);
    break;
  case 'A':
    text = locale_time_text(*data, TimeData::DAY, time->tm_wday);
    break;
  case 'b':
  case 'h':
    text = locale_time_text(*data,
                            section.modifier == ConvModifier::O
                                ? TimeData::ABALT_MON
                                : TimeData::ABMON,
                            time->tm_mon);
    break;
  case 'B':
    text = locale_time_text(
        *data,
        section.modifier == ConvModifier::O ? TimeData::ALT_MON : TimeData::MON,
        time->tm_mon);
    break;
  case 'p':
    text = locale_time_text(*data, TimeData::AM_PM, time->tm_hour >= 12);
    break;
  case 'c':
  case 'x':
  case 'X': {
    TimeData::Field ordinary =
        section.conv_name == 'c'   ? TimeData::DATE_TIME_FORMAT
        : section.conv_name == 'x' ? TimeData::DATE_FORMAT
                                   : TimeData::TIME_FORMAT;
    TimeData::Field alternative =
        section.conv_name == 'c'   ? TimeData::ERA_DATE_TIME_FORMAT
        : section.conv_name == 'x' ? TimeData::ERA_DATE_FORMAT
                                   : TimeData::ERA_TIME_FORMAT;
    auto selected = locale_time_text(
        *data, section.modifier == ConvModifier::E ? alternative : ordinary);
    if (selected.empty() && section.modifier == ConvModifier::E)
      selected = locale_time_text(*data, ordinary);
    format = selected;
    break;
  }
  case 'r':
    format = locale_time_text(*data, TimeData::AM_PM_FORMAT);
    break;
  default:
    break;
  }
  if (section.modifier == ConvModifier::O &&
      cpp::string_view("deHImMSuUVwWy").find_first_of(section.conv_name) !=
          cpp::string_view::npos) {
    auto number = get_int_format(section, time);
    if (!number)
      return -number.error();
    if (!number->sign_char &&
        number->num < data->count(TimeData::ALTERNATIVE_DIGITS)) {
      auto alternative =
          locale_time_text(*data, TimeData::ALTERNATIVE_DIGITS, number->num);
      if (!alternative.empty())
        text = alternative;
    }
  }
  if (section.modifier == ConvModifier::E &&
      (section.conv_name == 'C' || section.conv_name == 'y' ||
       section.conv_name == 'Y')) {
    auto era = select_era(*data, *time);
    if (era) {
      if (section.conv_name == 'C')
        text = era->name;
      else if (section.conv_name == 'Y')
        format = era->format;
      else {
        int64_t start = era->start.year + (era->start.year < 0);
        int64_t year = int64_t(time->tm_year) + 1900, delta, result;
        bool backwards = compare_era_dates(era->start, era->end) > 0;
        bool decreasing = (era->direction < 0) != backwards;
        if (__builtin_sub_overflow(year, start, &delta) ||
            (decreasing && __builtin_sub_overflow(int64_t(0), delta, &delta)) ||
            __builtin_add_overflow(era->offset, delta, &result))
          return -EOVERFLOW;
        IntFormatSection number;
        number.num =
            result < 0 ? uint64_t(0) - uint64_t(result) : uint64_t(result);
        number.sign_char = result < 0 ? '-' : 0;
        number.pad_to_len =
            section.min_width > 0 ? size_t(section.min_width) : 2;
        return write_padded_int(writer, number);
      }
    }
  }
  if (text) {
    size_t length = text->size();
    if (wide_output) {
      length = 0;
      // Category strings have already passed UTF-8 scalar validation.
      for (unsigned char byte : *text)
        length += (byte & 0xc0) != 0x80;
    }
    if (size_t(section.min_width) > length)
      RET_IF_RESULT_NEGATIVE(
          writer->write(' ', size_t(section.min_width) - length));
    return writer->write(*text);
  }
  if (format) {
    // Validated locale graphs have eight acyclic expansion nodes.
    if (depth >= 8)
      return -EINVAL;
    return format_time_sections(writer, format->data(), time, data, depth + 1,
                                wide_output);
  }
  return convert(writer, section, time);
}

template <printf_core::WriteMode mode>
int format_time_sections(printf_core::Writer<mode> *writer, const char *format,
                         const tm *time, const TimeData *data, unsigned depth,
                         bool wide_output) {
  Parser parser(format);
  for (auto section = parser.get_next_section(); !section.raw_string.empty();
       section = parser.get_next_section()) {
    int result = section.has_conv
                     ? convert_locale_time(writer, section, time, data, depth,
                                           wide_output)
                     : writer->write(section.raw_string);
    if (result < 0)
      return result;
  }
  return 0;
}
} // namespace strftime_core
} // namespace LIBC_NAMESPACE_DECL
#endif
