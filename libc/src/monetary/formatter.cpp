//===-- Locale monetary formatting ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/monetary/formatter.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/printf_core/float_dec_converter.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/string/memory_utils/inline_memset.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
using M = MonetaryLocaleData;

struct Output {
  char *data;
  size_t capacity;
  size_t size = 0;
  int write(cpp::string_view text) {
    if (text.size() > capacity - size)
      return -E2BIG;
    if (!text.empty())
      inline_memcpy(data + size, text.data(), text.size());
    size += text.size();
    return 0;
  }
  int write(char value, size_t count) {
    if (count > capacity - size)
      return -E2BIG;
    if (count)
      inline_memset(data + size, value, count);
    size += count;
    return 0;
  }
  int write(char value) { return write(value, 1); }
};

struct Format {
  char fill = ' ';
  bool grouping = true, plus = false, parentheses = false;
  bool currency = true, left = false, has_left = false, international = false;
  size_t width = 0, left_precision = 0;
  int precision = -1;
};

ErrorOr<size_t> decimal(const char *&cursor) {
  size_t result = 0;
  if (*cursor < '0' || *cursor > '9')
    return Error(EINVAL);
  while (*cursor >= '0' && *cursor <= '9') {
    unsigned digit = *cursor++ - '0';
    if (result > (cpp::numeric_limits<size_t>::max() - digit) / 10)
      return Error(E2BIG);
    result = result * 10 + digit;
  }
  return result;
}

ErrorOr<Format> parse(const char *&cursor) {
  Format result;
  for (;;) {
    switch (*cursor) {
    case '=':
      if (!*++cursor)
        return Error(EINVAL);
      result.fill = *cursor++;
      continue;
    case '^':
      result.grouping = false;
      break;
    case '+':
      result.plus = true;
      break;
    case '(':
      result.parentheses = true;
      break;
    case '!':
      result.currency = false;
      break;
    case '-':
      result.left = true;
      break;
    default:
      goto flags_done;
    }
    ++cursor;
  }
flags_done:
  if (result.plus && result.parentheses)
    return Error(EINVAL);
  if (*cursor >= '0' && *cursor <= '9') {
    auto width = decimal(cursor);
    if (!width)
      return Error(width.error());
    result.width = *width;
  }
  if (*cursor == '#') {
    ++cursor;
    auto left = decimal(cursor);
    if (!left)
      return Error(left.error());
    result.has_left = true;
    result.left_precision = *left;
  }
  if (*cursor == '.') {
    ++cursor;
    auto precision = decimal(cursor);
    if (!precision)
      return Error(precision.error());
    if (*precision > size_t(cpp::numeric_limits<int>::max()))
      return Error(E2BIG);
    result.precision = int(*precision);
  }
  if (*cursor != 'i' && *cursor != 'n')
    return Error(EINVAL);
  result.international = *cursor++ == 'i';
  return result;
}

cpp::string_view text(const M *data, M::Text field) {
  if (!data)
    return {};
  auto bytes = data->strings[field].bytes;
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size() - 1};
}
unsigned number(const M *data, M::Number field) {
  return data ? data->numbers[field] : 127;
}

struct Affix {
  cpp::string_view parts[6];
  unsigned count = 0;
  size_t size = 0;
  void append(cpp::string_view part) {
    if (part.empty())
      return;
    parts[count++] = part;
    size += part.size();
  }
  int write(Output &output) const {
    for (unsigned i = 0; i != count; ++i) {
      int result = output.write(parts[i]);
      if (result)
        return result;
    }
    return 0;
  }
};
struct Decoration {
  Affix before, after;
};

Decoration decorate(const M *data, const Format &format, bool negative) {
  const bool intl = format.international;
  unsigned precedes = number(
      data, intl ? (negative ? M::INT_N_CS_PRECEDES : M::INT_P_CS_PRECEDES)
                 : (negative ? M::N_CS_PRECEDES : M::P_CS_PRECEDES));
  unsigned spacing = number(
      data, intl ? (negative ? M::INT_N_SEP_BY_SPACE : M::INT_P_SEP_BY_SPACE)
                 : (negative ? M::N_SEP_BY_SPACE : M::P_SEP_BY_SPACE));
  unsigned position =
      number(data, intl ? (negative ? M::INT_N_SIGN_POSN : M::INT_P_SIGN_POSN)
                        : (negative ? M::N_SIGN_POSN : M::P_SIGN_POSN));
  auto sign = text(data, negative ? M::NEGATIVE_SIGN : M::POSITIVE_SIGN);
  if (position == 127) {
    position = 1;
    if (negative && text(data, M::NEGATIVE_SIGN).empty() &&
        text(data, M::POSITIVE_SIGN).empty())
      sign = "-";
  }
  if (format.plus && position == 0)
    position = 1;
  bool parentheses = format.parentheses ? negative : position == 0;
  if (format.parentheses || parentheses)
    sign = {};
  if (precedes == 127)
    precedes = 1;
  if (spacing == 127)
    spacing = 0;
  auto currency = format.currency ? text(data, intl ? M::INT_CURR_SYMBOL
                                                    : M::CURRENCY_SYMBOL)
                                  : cpp::string_view{};
  cpp::string_view separator = " ";
  if (intl && currency.size() == 4) {
    separator = currency.substr(3, 1);
    currency = currency.substr(0, 3);
  }
  Decoration result;
  if (parentheses)
    result.before.append("(");
  if (position == 1)
    result.before.append(sign);
  if (spacing == 2 && position == 1 && !precedes && !sign.empty())
    result.before.append(separator);
  Affix &side = precedes ? result.before : result.after;
  if (!precedes && spacing == 1 && !currency.empty())
    side.append(separator);
  if (position == 3)
    side.append(sign);
  bool sign_before_currency = position == 3 || (position == 1 && precedes);
  bool sign_after_currency = position == 4 || (position == 2 && !precedes);
  if (spacing == 2 && sign_before_currency && !sign.empty() &&
      !currency.empty())
    side.append(separator);
  side.append(currency);
  if (spacing == 2 && sign_after_currency && !sign.empty() && !currency.empty())
    side.append(separator);
  if (position == 4)
    side.append(sign);
  if (precedes && spacing == 1 && !currency.empty())
    side.append(separator);
  if (spacing == 2 && position == 2 && precedes && !sign.empty())
    result.after.append(separator);
  if (position == 2)
    result.after.append(sign);
  if (parentheses)
    result.after.append(")");
  return result;
}

ErrorOr<size_t> conversion(Output &output, Format format, double value,
                           const M *data) {
  if (format.plus && text(data, M::POSITIVE_SIGN).empty() &&
      text(data, M::NEGATIVE_SIGN).empty())
    return Error(EINVAL);
  if (format.width > output.capacity - output.size ||
      format.left_precision > output.capacity - output.size)
    return Error(E2BIG);
  if (format.precision < 0) {
    unsigned precision = number(data, format.international ? M::INT_FRAC_DIGITS
                                                           : M::FRAC_DIGITS);
    format.precision = precision == 127 ? 2 : int(precision);
  }
  if (size_t(format.precision) > output.capacity - output.size)
    return Error(E2BIG);

  fputil::FPBits<double> bits(value);
  bool negative = bits.is_neg();
  // 309 integer digits, sign and radix suffice for finite binary64. Extra
  // fractional precision is bounded by the caller's output capacity first.
  size_t bytes = size_t(format.precision) + 311;
  char local[512];
  struct Buffer {
    char *allocated = nullptr;
    ~Buffer() {
      if (allocated)
        ::free(allocated);
    }
  } storage;
  char *digits = local;
  if (bytes > sizeof(local)) {
    AllocChecker ac;
    storage.allocated = new (ac) char[bytes];
    if (!ac)
      return Error(ENOMEM);
    digits = storage.allocated;
  }
  Output raw{digits, bytes};
  if (bits.is_inf_or_nan()) {
    raw.write(bits.is_nan() ? "nan" : "inf");
  } else {
    printf_core::FormatSection section;
    section.precision = format.precision;
    section.conv_name = 'f';
    // Keep the sign during rounding so directed rounding remains correct.
    int result = printf_core::convert_float_decimal_typed<double>(
        &raw, section, bits, NumericParseLocale{});
    if (result < 0)
      return Error(-result);
  }
  size_t begin = negative && !bits.is_inf_or_nan() ? 1 : 0;
  size_t integer = begin;
  while (integer != raw.size && digits[integer] != '.')
    ++integer;
  cpp::string_view integral(digits + begin, integer - begin);
  if (integral.size() > format.left_precision)
    format.has_left = false;
  NumericParseLocale selected;
  auto radix = text(data, M::DECIMAL_POINT);
  selected.radix = radix.empty() ? cpp::string_view(".") : radix;
  selected.thousands = text(data, M::THOUSANDS_SEP);
  if (data)
    selected.grouping = data->grouping.bytes;
  printf_core::NumericGrouping<Output> grouping(
      format.grouping && !bits.is_inf_or_nan(), selected);
  size_t extra = grouping.extra_size(integral.size());
  size_t fill = 0;
  if (format.left_precision > integral.size()) {
    size_t separators = grouping.separator_count(format.left_precision);
    size_t unit = grouping.separator_size();
    size_t available = output.capacity - output.size;
    if (unit && separators > available / unit)
      return Error(E2BIG);
    fill = format.left_precision - integral.size();
    size_t added = separators * unit - extra;
    if (added > available - fill)
      return Error(E2BIG);
    fill += added;
  }
  auto decoration = decorate(data, format, negative);
  size_t before = decoration.before.size, after = decoration.after.size;
  if (format.has_left) {
    auto opposite = decorate(data, format, !negative);
    if (opposite.before.size > before)
      before = opposite.before.size;
    if (opposite.after.size > after)
      after = opposite.after.size;
  }
  size_t fraction = integer < raw.size ? raw.size - integer - 1 : 0;
  size_t size = 0;
  const size_t pieces[] = {before,  after,
                           fill,    integral.size(),
                           extra,   fraction ? selected.radix.size() : 0,
                           fraction};
  for (size_t piece : pieces) {
    if (piece > output.capacity - output.size - size)
      return Error(E2BIG);
    size += piece;
  }
  size_t padding = format.width > size ? format.width - size : 0;
  if (size > output.capacity - output.size ||
      padding > output.capacity - output.size - size)
    return Error(E2BIG);
  if (!format.left)
    output.write(' ', padding);
  output.write(' ', before - decoration.before.size);
  decoration.before.write(output);
  output.write(format.fill, fill);
  grouping.write(&output, integral);
  if (fraction) {
    output.write(selected.radix);
    output.write({digits + integer + 1, fraction});
  }
  decoration.after.write(output);
  output.write(' ', after - decoration.after.size);
  if (format.left)
    output.write(' ', padding);
  return output.size;
}
} // namespace

ErrorOr<size_t> format_money(char *output, size_t capacity, const char *format,
                             ArgList &args, const M *data) {
  if (!capacity)
    return Error(E2BIG);
  if (capacity > size_t(cpp::numeric_limits<ptrdiff_t>::max()))
    return Error(EOVERFLOW);
  Output writer{output, capacity - 1};
  while (*format) {
    if (*format != '%') {
      if (int error = writer.write(*format++))
        return Error(-error);
      continue;
    }
    ++format;
    if (*format == '%') {
      ++format;
      if (int error = writer.write('%'))
        return Error(-error);
      continue;
    }
    auto spec = parse(format);
    if (!spec)
      return Error(spec.error());
    auto result = conversion(writer, *spec, args.next_var<double>(), data);
    if (!result)
      return Error(result.error());
  }
  output[writer.size] = '\0';
  return writer.size;
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
