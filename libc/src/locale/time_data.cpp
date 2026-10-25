//===-- Validated time category text tables -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/time_data.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/locale/category_validation.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
using Text = cpp::string_view;

Text view(const LocaleDataView::Item &item) {
  return {reinterpret_cast<const char *>(item.bytes.data()),
          item.bytes.size() - 1};
}

bool number(Text text, int64_t &value) {
  bool negative = !text.empty() && text[0] == '-';
  if (!text.empty() && (text[0] == '-' || text[0] == '+'))
    text.remove_prefix(1);
  if (text.empty())
    return false;
  uint64_t magnitude = 0;
  uint64_t limit = uint64_t(cpp::numeric_limits<int64_t>::max()) + negative;
  for (char c : text) {
    if (c < '0' || c > '9' || magnitude > (limit - (c - '0')) / 10)
      return false;
    magnitude = magnitude * 10 + (c - '0');
  }
  value = negative ? -int64_t(magnitude - (magnitude != 0)) - (magnitude != 0)
                   : int64_t(magnitude);
  return true;
}

bool date(Text text, TimeLocaleData::Date &result, bool endpoint) {
  if (endpoint && (text == "-*" || text == "+*")) {
    result = {0, 0, 0, text[0] == '-' ? -1 : 1};
    return true;
  }
  size_t slash = text.find_first_of('/');
  int64_t year, month, day;
  if (slash == Text::npos || !number(text.substr(0, slash), year) || !year)
    return false;
  text.remove_prefix(slash + 1);
  slash = text.find_first_of('/');
  if (slash == Text::npos || !number(text.substr(0, slash), month) ||
      !number(text.substr(slash + 1), day) || month < 1 || month > 12 ||
      day < 1)
    return false;
  // Era dates have no year zero: 1 BC is astronomical year zero.
  int64_t astronomical = year < 0 ? year + 1 : year;
  bool leap = astronomical % 4 == 0 &&
              (astronomical % 100 != 0 || astronomical % 400 == 0);
  constexpr unsigned DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > DAYS[month - 1] + (month == 2 && leap))
    return false;
  result = {year, unsigned(month), unsigned(day)};
  return true;
}

bool contains(Text set, char c) { return set.find_first_of(c) != Text::npos; }

// Nodes 0..6 are the ordinary/alternative formats; node 7 is the union of
// era-year formats. Edges model all possible locale-dependent expansions.
bool dependencies(Text format, unsigned &edges) {
  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%')
      continue;
    ++i;
    if (i < format.size() && contains("+0", format[i]))
      ++i;
    unsigned width = 0;
    while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
      unsigned digit = format[i++] - '0';
      if (width > (unsigned(cpp::numeric_limits<int>::max()) - digit) / 10)
        return false;
      width = width * 10 + digit;
    }
    char modifier = 0;
    if (i < format.size() && contains("EO", format[i]))
      modifier = format[i++];
    if (i == format.size())
      return false;
    char c = format[i];
    if (!contains("aAbBcCdDeFgGhHIjmMnprRsStTuUVwWxXyYzZ%", c) ||
        (modifier == 'E' && !contains("cCxXyY", c)) ||
        (modifier == 'O' && !contains("bBdeHImMSuUVwWy", c)))
      return false;
    if (c == 'c')
      edges |= 1U << (modifier == 'E' ? 6 : 0);
    if (c == 'x')
      edges |= 1U << (modifier == 'E' ? 4 : 1);
    if (c == 'X')
      edges |= 1U << (modifier == 'E' ? 5 : 2);
    if (c == 'r')
      edges |= 1U << 3;
    if (c == 'Y' && modifier == 'E')
      edges |= 1U << 7;
  }
  return true;
}
} // namespace

ErrorOr<TimeLocaleData::Era> TimeLocaleData::parse_era(Text text) {
  Text parts[6];
  for (unsigned i = 0; i != 5; ++i) {
    size_t colon = text.find_first_of(':');
    if (colon == Text::npos)
      return Error(EINVAL);
    parts[i] = text.substr(0, colon);
    text.remove_prefix(colon + 1);
  }
  parts[5] = text;
  Era result{};
  if ((parts[0] != "+" && parts[0] != "-") ||
      !number(parts[1], result.offset) ||
      !date(parts[2], result.start, false) ||
      !date(parts[3], result.end, true) || parts[4].empty() || parts[5].empty())
    return Error(EINVAL);
  result.direction = parts[0] == "+" ? 1 : -1;
  result.name = parts[4];
  result.format = parts[5];
  return result;
}

ErrorOr<TimeLocaleData::Era> TimeLocaleData::era(size_t index) const {
  auto item = text(ERAS, index);
  if (!item)
    return Error(item.error());
  return parse_era(view(*item));
}

bool TimeLocaleData::valid_formats() const {
  constexpr Field FIELDS[] = {
      DATE_TIME_FORMAT, DATE_FORMAT,     TIME_FORMAT,         AM_PM_FORMAT,
      ERA_DATE_FORMAT,  ERA_TIME_FORMAT, ERA_DATE_TIME_FORMAT};
  unsigned edges[8]{};
  for (unsigned i = 0; i != 7; ++i) {
    Text format = view(text(FIELDS[i], 0).value());
    if (!dependencies(format, edges[i]))
      return false;
    if (format.empty() && i >= 4)
      edges[i] = 1U << (i == 6 ? 0 : i - 3);
  }
  bool am = !view(text(AM_PM, 0).value()).empty();
  bool pm = !view(text(AM_PM, 1).value()).empty();
  if (am != pm || am != !view(text(AM_PM_FORMAT, 0).value()).empty())
    return false;
  for (size_t i = 0; i != count(ERAS); ++i) {
    auto record = era(i);
    if (!record || !dependencies(record->format, edges[7]))
      return false;
  }
  // Transitive closure is bounded by eight nodes, independent of file size.
  for (unsigned k = 0; k != 8; ++k)
    for (unsigned i = 0; i != 8; ++i)
      if (edges[i] & (1U << k))
        edges[i] |= edges[k];
  for (unsigned i = 0; i != 8; ++i)
    if (edges[i] & (1U << i))
      return false;
  return true;
}

ErrorOr<TimeLocaleData> TimeLocaleData::read(const LocaleDataView &file) {
  TimeLocaleData result(file);
  constexpr unsigned REQUIRED[] = {7, 7, 12, 12, 12, 12, 2, 1,
                                   1, 1, 1,  0,  1,  1,  1, 0};
  for (unsigned field = 0; field != FIELD_COUNT; ++field) {
    uint32_t key = (field + 1) << 16;
    auto item = file.find(2, key);
    if (!item || item->kind != LocaleDataView::Kind::WORDS ||
        item->bytes.size() != 4)
      return Error(EINVAL);
    unsigned count = item->word(0).value();
    if ((REQUIRED[field] && count != REQUIRED[field]) ||
        (field == ERAS && count > 1024) ||
        (field == ALTERNATIVE_DIGITS && count > 100))
      return Error(EINVAL);
    result.counts[field] = count;
    for (unsigned i = 0; i != count; ++i) {
      auto text = file.find(2, key + i + 1);
      if (!text ||
          !locale_utf8_string(*text, field < AM_PM ? 1 : 0, size_t(-1)))
        return Error(EINVAL);
    }
  }
  if (!result.valid_formats())
    return Error(EINVAL);
  return result;
}

size_t TimeLocaleData::count(Field field) const {
  return field < FIELD_COUNT ? counts[field] : 0;
}

ErrorOr<LocaleDataView::Item> TimeLocaleData::text(Field field,
                                                   size_t index) const {
  if (field >= FIELD_COUNT || index >= counts[field])
    return Error(EINVAL);
  return file.find(2, ((uint32_t(field) + 1) << 16) + uint32_t(index) + 1);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
