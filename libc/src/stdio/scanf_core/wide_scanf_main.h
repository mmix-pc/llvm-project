//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_SCANF_CORE_WIDE_SCANF_MAIN_H
#define LLVM_LIBC_SRC_STDIO_SCANF_CORE_WIDE_SCANF_MAIN_H

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/wchar_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/arg_list.h"
#include "src/__support/char_vector.h"
#include "src/__support/libc_errno.h"
#include "src/__support/wchar/wcrtomb.h"
#include "src/__support/wctype_utils.h"
#include "src/stdio/scanf_core/converter_utils.h"
#include "src/stdio/scanf_core/string_output.h"
#ifndef LIBC_COPT_SCANF_DISABLE_FLOAT
#include "src/stdio/scanf_core/float_converter.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace scanf_core {

// Keep one wide lookahead, including embedded NUL, separate from end of input.
// Only consumed characters contribute to %n; sources retain unread lookahead.
template <typename Source> class WideReader {
  Source &source;
  wint_t next = WEOF;
  bool cached = false;
  size_t count = 0;

public:
  explicit WideReader(Source &source) : source(source) {}
  wint_t peek() {
    if (!cached) {
      next = source.peek();
      cached = true;
    }
    return next;
  }
  void take() {
    if (peek() != WEOF) {
      source.take();
      ++count;
      cached = false;
    }
  }
  size_t consumed() const { return count; }
};

LIBC_INLINE bool wide_scan_space(wint_t c) {
  return c != WEOF && internal::isspace(static_cast<wchar_t>(c));
}

LIBC_INLINE int wide_scan_digit(wint_t c) {
  if (c >= L'0' && c <= L'9')
    return int(c - L'0');
  if (c >= L'a' && c <= L'f')
    return int(c - L'a') + 10;
  if (c >= L'A' && c <= L'F')
    return int(c - L'A') + 10;
  return -1;
}

LIBC_INLINE wint_t wide_scan_lower(wint_t c) {
  return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
}

enum class WideScanResult {
  OK,
  MATCH_FAILURE,
  INPUT_FAILURE,
  ENCODING_FAILURE,
  ALLOCATION_FAILURE
};

template <typename Source>
WideScanResult wide_scan_integer(WideReader<Source> &input, size_t width,
                                 const FormatSection &section) {
  if (input.peek() == WEOF)
    return WideScanResult::INPUT_FAILURE;
  auto take = [&] {
    input.take();
    --width;
  };
  if (section.conv_name == 'p' && input.peek() == L'(') {
    const char null_pointer[] = "(nullptr)";
    for (char c : null_pointer) {
      if (!c)
        break;
      if (!width || wide_scan_lower(input.peek()) != wint_t(c))
        return WideScanResult::MATCH_FAILURE;
      take();
    }
    write_int_with_length(0, section);
    return WideScanResult::OK;
  }
  bool negative = input.peek() == L'-';
  if (input.peek() == L'+' || negative)
    take();
  int base = section.conv_name == 'o'   ? 8
             : section.conv_name == 'i' ? 0
             : section.conv_name == 'x' || section.conv_name == 'X' ||
                     section.conv_name == 'p'
                 ? 16
                 : 10;
  bool digits = false;
  uintmax_t value = 0;
  if (width && input.peek() == L'0' && (base == 0 || base == 16)) {
    take();
    digits = true;
    if (width && wide_scan_lower(input.peek()) == L'x') {
      take();
      base = 16;
      digits = false;
    } else if (base == 0) {
      base = 8;
    }
  }
  if (base == 0)
    base = 10;
  constexpr auto limit = cpp::numeric_limits<uintmax_t>::max();
  while (width) {
    int digit = wide_scan_digit(input.peek());
    if (digit < 0 || digit >= base)
      break;
    digits = true;
    value = value > (limit - uintmax_t(digit)) / uintmax_t(base)
                ? limit
                : value * uintmax_t(base) + uintmax_t(digit);
    take();
  }
  if (!digits)
    return WideScanResult::MATCH_FAILURE;
  write_int_with_length(negative ? -value : value, section);
  return WideScanResult::OK;
}

#ifndef LIBC_COPT_SCANF_DISABLE_FLOAT
template <typename Source>
WideScanResult wide_scan_float(WideReader<Source> &input, size_t width,
                               const FormatSection &section) {
  int result = scan_float_token(
      [&]() -> int {
        wint_t c = input.peek();
        return c == WEOF ? -1 : c <= 127 ? int(c) : 256;
      },
      [&] { input.take(); }, width, section);
  return result == READ_OK              ? WideScanResult::OK
         : result == MATCHING_FAILURE   ? WideScanResult::MATCH_FAILURE
         : result == ALLOCATION_FAILURE ? WideScanResult::ALLOCATION_FAILURE
                                        : WideScanResult::INPUT_FAILURE;
}
#endif

struct WideScanSet {
  const wchar_t *begin = nullptr;
  const wchar_t *end = nullptr;
  bool inverted = false;

  bool contains(wint_t c) const {
    bool found = false;
    for (auto p = begin; p != end; ++p) {
      if (end - p >= 3 && p[1] == L'-') {
        found |= c >= wint_t(p[0]) && c <= wint_t(p[2]);
        p += 2;
      } else {
        found |= c == wint_t(*p);
      }
    }
    return found != inverted;
  }
};

template <typename CharT, typename Source>
WideScanResult wide_scan_string_to(WideReader<Source> &input, size_t width,
                                   const FormatSection &section,
                                   const WideScanSet &set) {
  size_t count = 0;
  StringOutput<CharT> output(section);
  internal::mbstate state{};
  while (count < width && input.peek() != WEOF) {
    wint_t c = input.peek();
    if ((section.conv_name == 's' && wide_scan_space(c)) ||
        (section.conv_name == '[' && !set.contains(c)))
      break;
    if constexpr (sizeof(CharT) == sizeof(wchar_t)) {
      if (!output.append(static_cast<wchar_t>(c)))
        return WideScanResult::ALLOCATION_FAILURE;
    } else {
      char bytes[4];
      auto result = internal::wcrtomb(bytes, static_cast<wchar_t>(c), &state);
      if (!result.has_value()) {
        libc_errno = result.error();
        return WideScanResult::ENCODING_FAILURE;
      }
      for (size_t i = 0; i < result.value(); ++i)
        if (!output.append(bytes[i]))
          return WideScanResult::ALLOCATION_FAILURE;
    }
    input.take();
    ++count;
  }
  if (!count)
    return input.peek() == WEOF ? WideScanResult::INPUT_FAILURE
                                : WideScanResult::MATCH_FAILURE;
  if (section.conv_name == 'c' && count != width)
    return WideScanResult::INPUT_FAILURE;
  if (section.conv_name != 'c' && !output.append(CharT(0)))
    return WideScanResult::ALLOCATION_FAILURE;
  output.publish();
  return WideScanResult::OK;
}

template <typename Source>
WideScanResult wide_scan_string(WideReader<Source> &input, size_t width,
                                const FormatSection &section,
                                const WideScanSet &set) {
  if (section.length_modifier == LengthModifier::l)
    return wide_scan_string_to<wchar_t>(input, width, section, set);
  return wide_scan_string_to<char>(input, width, section, set);
}

template <typename Source>
int wide_scanf_main(Source &source, const wchar_t *format,
                    internal::ArgList &args, int *conversion_status = nullptr) {
  WideReader<Source> input(source);
#ifndef LIBC_COPT_SCANF_DISABLE_INDEX_MODE
  internal::ArgList args_start(args);
#endif
  int assignments = 0;
  bool converted = false;
  if (conversion_status)
    *conversion_status = READ_OK;
  auto failure = [&](WideScanResult status) {
    if (conversion_status)
      *conversion_status =
          status == WideScanResult::ENCODING_FAILURE     ? ENCODING_FAILURE
          : status == WideScanResult::ALLOCATION_FAILURE ? ALLOCATION_FAILURE
          : status == WideScanResult::INPUT_FAILURE      ? FILE_READ_ERROR
                                                         : MATCHING_FAILURE;
    return status != WideScanResult::MATCH_FAILURE && !converted ? EOF
                                                                 : assignments;
  };
  while (*format) {
    if (wide_scan_space(wint_t(*format))) {
      ++format;
      while (wide_scan_space(input.peek()))
        input.take();
      continue;
    }
    if (*format != L'%') {
      if (input.peek() == WEOF)
        return failure(WideScanResult::INPUT_FAILURE);
      if (input.peek() != wint_t(*format))
        return assignments;
      input.take();
      ++format;
      continue;
    }
    ++format;
    if (*format == L'%') {
      if (input.peek() == WEOF)
        return failure(WideScanResult::INPUT_FAILURE);
      if (input.peek() != L'%')
        return assignments;
      input.take();
      ++format;
      converted = true;
      continue;
    }
    FormatSection section{};
#ifndef LIBC_COPT_SCANF_DISABLE_INDEX_MODE
    size_t index = 0;
    const wchar_t *index_start = format;
    while (*format >= L'0' && *format <= L'9') {
      size_t digit = size_t(*format++ - L'0');
      constexpr auto max = cpp::numeric_limits<size_t>::max();
      index = index > (max - digit) / 10 ? max : index * 10 + digit;
    }
    if (*format == L'$') {
      if (!index)
        return assignments;
      ++format;
    } else {
      index = 0;
      format = index_start;
    }
#endif
    if (*format == L'*') {
      section.flags = NO_WRITE;
      ++format;
    }
    size_t width = 0;
    bool has_width = false;
    while (*format >= L'0' && *format <= L'9') {
      has_width = true;
      size_t digit = size_t(*format++ - L'0');
      constexpr auto max = cpp::numeric_limits<size_t>::max();
      width = width > (max - digit) / 10 ? max : width * 10 + digit;
    }
    if (*format == L'm') {
      section.flags = FormatFlags(section.flags | ALLOCATE);
      ++format;
    }
    if (*format == L'h') {
      section.length_modifier = LengthModifier::h;
      if (*++format == L'h') {
        section.length_modifier = LengthModifier::hh;
        ++format;
      }
    } else if (*format == L'l') {
      section.length_modifier = LengthModifier::l;
      if (*++format == L'l') {
        section.length_modifier = LengthModifier::ll;
        ++format;
      }
    } else {
      switch (*format) {
      case L'j':
        section.length_modifier = LengthModifier::j;
        ++format;
        break;
      case L'z':
        section.length_modifier = LengthModifier::z;
        ++format;
        break;
      case L't':
        section.length_modifier = LengthModifier::t;
        ++format;
        break;
      case L'L':
        section.length_modifier = LengthModifier::L;
        ++format;
        break;
      }
    }
    if (!*format || *format > 127 || (has_width && width == 0))
      return assignments;
    section.conv_name = static_cast<char>(*format++);
    if (section.conv_name == 'C' || section.conv_name == 'S') {
      section.conv_name = section.conv_name == 'C' ? 'c' : 's';
      section.length_modifier = LengthModifier::l;
    }
    WideScanSet set;
    if (section.conv_name == '[') {
      if (*format == L'^') {
        set.inverted = true;
        ++format;
      }
      set.begin = format;
      if (*format == L']')
        ++format;
      while (*format && *format != L']')
        ++format;
      if (!*format)
        return assignments;
      set.end = format++;
    }
    if ((section.flags & NO_WRITE) == 0) {
#ifndef LIBC_COPT_SCANF_DISABLE_INDEX_MODE
      if (index) {
        internal::ArgList indexed(args_start);
        for (size_t i = 1; i < index; ++i)
          indexed.next_var<void *>();
        section.output_ptr = indexed.next_var<void *>();
      } else
#endif
        section.output_ptr = args.next_var<void *>();
    }
    if (section.conv_name == 'n') {
      write_int_with_length(input.consumed(), section);
      converted = true;
      continue;
    }
    if (!has_width)
      width = section.conv_name == 'c' ? 1 : cpp::numeric_limits<size_t>::max();
    if (section.conv_name != 'c' && section.conv_name != '[')
      while (wide_scan_space(input.peek()))
        input.take();
    WideScanResult result;
    switch (section.conv_name) {
    case 's':
    case 'c':
    case '[':
      result = wide_scan_string(input, width, section, set);
      break;
    case 'd':
    case 'i':
    case 'u':
    case 'o':
    case 'x':
    case 'X':
    case 'p':
      result = wide_scan_integer(input, width, section);
      break;
#ifndef LIBC_COPT_SCANF_DISABLE_FLOAT
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'a':
    case 'A':
    case 'g':
    case 'G':
      result = wide_scan_float(input, width, section);
      break;
#endif
    default:
      return assignments;
    }
    if (result != WideScanResult::OK)
      return failure(result);
    converted = true;
    if ((section.flags & NO_WRITE) == 0)
      ++assignments;
  }
  return assignments;
}

} // namespace scanf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_SCANF_CORE_WIDE_SCANF_MAIN_H
