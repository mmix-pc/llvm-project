//===-- Tests for wide printf conversion
//-----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/arg_list.h"
#include "src/__support/printf_core/parser.h"
#include "src/__support/printf_core/wide_converter.h"
#include "src/stdio/snprintf.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"

#include <stdarg.h>

using namespace LIBC_NAMESPACE::printf_core;
using LIBC_NAMESPACE::cpp::basic_string_view;
using LIBC_NAMESPACE::internal::ArgList;

namespace {

// Exercise internal parsing/conversion, not the public swprintf placeholder.
int format_to(wchar_t *buffer, size_t capacity, const wchar_t *format, ...) {
  va_list vlist;
  va_start(vlist, format);
  ArgList args(vlist);
  va_end(vlist);
  Parser<ArgList, wchar_t> parser(format, args);
  WideWriter writer(buffer, capacity);
  for (auto section = parser.get_next_section(); !section.raw_string.empty();
       section = parser.get_next_section()) {
    int result = convert_wide(&writer, section);
    if (result != WRITE_OK)
      return result;
  }
  return writer.get_error() == WRITE_OK
             ? static_cast<int>(writer.get_chars_written())
             : writer.get_error();
}

template <typename... Args>
void check(const wchar_t *expected, const wchar_t *format, Args... args) {
  wchar_t buffer[2048];
  int count = format_to(buffer, 2048, format, args...);
  ASSERT_EQ(count,
            static_cast<int>(basic_string_view<wchar_t>(expected).size()));
  EXPECT_TRUE(basic_string_view<wchar_t>(buffer) ==
              basic_string_view<wchar_t>(expected));
}

} // namespace

TEST(LlvmLibcWidePrintfConverterTest, LiteralAndPercent) {
  check(L"\u4e2d%\u03bb", L"\u4e2d%%\u03bb");
}

#ifndef LIBC_COPT_PRINTF_DISABLE_WIDE
TEST(LlvmLibcWidePrintfConverterTest, Characters) {
  check(L"A \u4e2d", L"%c %lc", int('A'), wint_t(L'\u4e2d'));
  check(L"  A|\u03bb  ", L"%3c|%-3lc", int('A'), wint_t(L'\u03bb'));
  check(L"A", L"%c", 0x141); // Conversion to unsigned char precedes btowc.
  wchar_t buffer[8];
  EXPECT_EQ(format_to(buffer, 8, L"%c%lcX", 0, wint_t(0)), 3);
  EXPECT_EQ(buffer[0], L'\0');
  EXPECT_EQ(buffer[1], L'\0');
  EXPECT_EQ(buffer[2], L'X');
  EXPECT_EQ(buffer[3], L'\0');
}

TEST(LlvmLibcWidePrintfConverterTest, InvalidSingleBytes) {
  const int bytes[] = {0x80, 0xc2, 0xff};
  for (int byte : bytes) {
    wchar_t buffer[8];
    EXPECT_EQ(format_to(buffer, 8, L"A%c", byte), MB_CONVERSION_ERROR);
    EXPECT_TRUE(basic_string_view<wchar_t>(buffer) == L"A");
  }
}

TEST(LlvmLibcWidePrintfConverterTest, Strings) {
  check(L"abc|\u4e2d\u03bb", L"%s|%ls", "abc", L"\u4e2d\u03bb");
  check(L"\u4e2d\u03bb\U0001f642", L"%s",
        "\xe4\xb8\xad\xce\xbb\xf0\x9f\x99\x82");
  check(L"   \u4e2d|\u03bb   ", L"%4s|%-4ls", "\xe4\xb8\xad", L"\u03bb");
  check(L"  |   ", L"%2s|%-3ls", "", L"");
}

TEST(LlvmLibcWidePrintfConverterTest, StringPrecision) {
  check(L"\u4e2d|\u03bb", L"%.1s|%.1ls", "\xe4\xb8\xadX", L"\u03bbX");
  check(L"  \u4e2d|\u03bb  ", L"%*.*s|%-*.*ls", 3, 1, "\xe4\xb8\xadX", 3, 1,
        L"\u03bbX");
  check(L"abc", L"%.*s", -1, "abc");
  // These arrays deliberately have no terminator within the selected prefix.
  const wchar_t wide[] = {L'A', L'B'};
  const char bytes[] = {'\xe4', '\xb8', '\xad'};
  check(L"AB|\u4e2d", L"%.2ls|%.1s", wide, bytes);
  check(L"|", L"%.0s|%.0ls", bytes, wide);
  check(L"A", L"%.1s", "A\xff");
}

TEST(LlvmLibcWidePrintfConverterTest, InvalidStrings) {
  const char *invalid[] = {
      "\x80",         "\xe4\xb8",        "\xc2X",        "A\xff",
      "\xc0\x80",     "\xc0\xaf",        "\xe0\x80\xaf", "\xf0\x80\x80\xaf",
      "\xed\xa0\x80", "\xf4\x90\x80\x80"};
  for (const char *text : invalid) {
    wchar_t buffer[16];
    EXPECT_EQ(format_to(buffer, 16, L"prefix:%s", text), MB_CONVERSION_ERROR);
    EXPECT_TRUE(basic_string_view<wchar_t>(buffer) == L"prefix:");
  }
}

TEST(LlvmLibcWidePrintfConverterTest, IndependentStringState) {
  check(L"\u4e2d\u03bb", L"%s%s", "\xe4\xb8\xad", "\xce\xbb");
  wchar_t buffer[8];
  EXPECT_EQ(format_to(buffer, 8, L"%s", "\xe4"), MB_CONVERSION_ERROR);
  check(L"\u03bb", L"%s", "\xce\xbb");
}

TEST(LlvmLibcWidePrintfConverterTest, StringAndCharacterTruncation) {
  wchar_t buffer[] = {L'!', L'!', L'!', L'!', L'!'};
  EXPECT_EQ(format_to(buffer + 1, 3, L"%s",
                      "A\xe4\xb8\xad"
                      "B"),
            BUFFER_TOO_SMALL);
  EXPECT_EQ(buffer[0], L'!');
  EXPECT_EQ(buffer[4], L'!');
  EXPECT_TRUE(basic_string_view<wchar_t>(buffer + 1) == L"A\u4e2d");
  EXPECT_EQ(format_to(buffer, 2, L"%2lc", wint_t(L'\u4e2d')), BUFFER_TOO_SMALL);
  EXPECT_TRUE(basic_string_view<wchar_t>(buffer) == L" ");
  EXPECT_EQ(format_to(buffer, 3, L"%-4ls", L"A"), BUFFER_TOO_SMALL);
  EXPECT_TRUE(basic_string_view<wchar_t>(buffer) == L"A ");
#ifndef LIBC_COPT_PRINTF_DISABLE_WRITE_INT
  int count = -1;
  check(L"\u4e2d\u03bb", L"%s%ls%n", "\xe4\xb8\xad", L"\u03bb", &count);
  EXPECT_EQ(count, 2);
  count = -1;
  EXPECT_EQ(format_to(buffer, 2, L"%s%n", "\xff", &count), MB_CONVERSION_ERROR);
  EXPECT_EQ(count, -1);
#endif
}
#endif

TEST(LlvmLibcWidePrintfConverterTest, IntegerValuesAndLengths) {
  check(L"-128 -32768 -2147483648", L"%hhd %hd %d", -128, -32768,
        (-2147483647 - 1));
  check(L"-9223372036854775808", L"%lld", (-9223372036854775807LL - 1));
  check(L"18446744073709551615", L"%llu", 18446744073709551615ULL);
  check(L"255 65535 4294967295", L"%hhu %hu %u", 255, 65535, 4294967295U);
  check(L"-1 -2 3 -4", L"%ld %jd %zu %td", -1L, intmax_t(-2), size_t(3),
        ptrdiff_t(-4));
}

TEST(LlvmLibcWidePrintfConverterTest, IntegerPaddingAndPrecision) {
  // Retain representative expectations from stdio/sprintf_test.cpp.
  check(L"007F 0x1000000000 002   ", L"%04X %#llx %-6.3zu", 127U,
        0x1000000000ULL, size_t(2));
  check(L"+001", L"%+04d", 1);
  check(L"   001", L"%06.3d", 1);
  check(L"001   ", L"%-*.*d", 6, 3, 1);
  check(L"1     ", L"%*.*d", -6, -3, 1);
  check(L" 1 +1", L"% d %+ d", 1, 1);
  check(L"|0|", L"%.0d|%#.0o|%.0x", 0, 0U, 0U);
}

TEST(LlvmLibcWidePrintfConverterTest, BasesAndPrefixes) {
  check(L"0x1f 0X1F 037 0b11111 0B11111", L"%#x %#X %#o %#b %#B", 31U, 31U, 31U,
        31U, 31U);
#ifndef LIBC_COPT_PRINTF_DISABLE_BITINT
  check(L"1 2", L"%w8u %wf16u", 257U, 65538U);
#endif
}

#ifndef LIBC_COPT_PRINTF_DISABLE_FLOAT
template <typename T>
void check_float_boundaries(const char *format, const wchar_t *wide_format) {
  using Bits = LIBC_NAMESPACE::fputil::FPBits<T>;
  const T values[] = {Bits::min_subnormal().get_val(),
                      Bits::min_normal().get_val(),
                      Bits::max_normal().get_val()};
  for (T value : values) {
    char narrow[2048];
    wchar_t wide[2048];
    int count = LIBC_NAMESPACE::snprintf(narrow, 2048, format, value);
    ASSERT_GT(count, 0);
    ASSERT_LT(count, 2048);
    ASSERT_EQ(format_to(wide, 2048, wide_format, value), count);
    for (int i = 0; i <= count; ++i)
      EXPECT_EQ(wide[i], static_cast<wchar_t>(narrow[i]));
  }
}

TEST(LlvmLibcWidePrintfConverterTest, FloatingFormats) {
  using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  ForceRoundingMode rounding(RoundingMode::Nearest);
  ASSERT_TRUE(rounding.success);
  check(L"1.500000 -0.000000", L"%f %f", 1.5, -0.0);
  check(L"+0001.50 1.5     ", L"%+08.2f %-8.2g", 1.5, 1.5);
  check(L"1.500000e+00 1.500000E+00", L"%e %E", 1.5, 1.5);
  check(L"1.000000e+100 1.000000e-100", L"%e %e", 1.0e100, 1.0e-100);
  check(L"1e+06 1E+06 1.00", L"%.1g %.1G %#.3g", 1.0e6, 1.0e6, 1.0);
  check(L"1. 0.", L"%#.0f %#.0f", 1.0, 0.0);
  check(L"2 4", L"%.0f %.0f", 2.5, 3.5);
  check(L"10.00", L"%.2f", 9.999);
  check(L"0.0000000000000000000000001", L"%.25f", 1.0e-25);
}

TEST(LlvmLibcWidePrintfConverterTest, FloatingHexAndExtremes) {
  using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  ForceRoundingMode rounding(RoundingMode::Nearest);
  ASSERT_TRUE(rounding.success);
  // Reuse exact FloatHexExpConv vectors from stdio/sprintf_test.cpp.
  check(L"0x1p+0 -0X1P+0", L"%a %A", 1.0, -1.0);
  check(L"-0x1.abcdef12345p+0", L"%a", -0x1.abcdef12345p0);
  check(L"0x1.249ad2594c37dp+332", L"%a", 1.0e100);
  check(L"0x0.08p-1022", L"%a", 0x1.0p-1027);
  check(L"0x0.0000000000001p-1022", L"%a", 0x1.0p-1074);
  check(L"0x1.fffffffffffffp+1023", L"%a", 0x1.fffffffffffffp1023);
  check(L"0x1.80p+0", L"%.2a", 1.5);
}

TEST(LlvmLibcWidePrintfConverterTest, InfAndNaN) {
  using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;
  check(L"inf -INF", L"%f %F", Bits::inf().get_val(), -Bits::inf().get_val());
  check(L"nan NAN", L"%g %A", Bits::quiet_nan().get_val(),
        Bits::quiet_nan().get_val());
  check(L"    +inf", L"%+08f", Bits::inf().get_val());
}

TEST(LlvmLibcWidePrintfConverterTest, FloatingRoundingModes) {
  using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Downward,
                                RoundingMode::Upward, RoundingMode::TowardZero};
  const wchar_t *expected[] = {L"2 -2", L"1 -2", L"2 -1", L"1 -1"};
  for (size_t i = 0; i < 4; ++i) {
    ForceRoundingMode rounding(modes[i]);
    if (rounding.success)
      check(expected[i], L"%.0f %.0f", 1.5, -1.5);
  }
}

#ifndef LIBC_TYPES_LONG_DOUBLE_IS_DOUBLE_DOUBLE
TEST(LlvmLibcWidePrintfConverterTest, LongDoubleFormats) {
  check(L"1.500000 -0.000000", L"%Lf %Lf", 1.5L, -0.0L);
  check(L"1.500000e+00 1.5", L"%Le %Lg", 1.5L, 1.5L);
  check(L"1.25000000000000000000", L"%.20Lf", 1.25L);
  check_float_boundaries<long double>("%.20Le", L"%.20Le");
  check_float_boundaries<long double>("%.20Lg", L"%.20Lg");
  check_float_boundaries<long double>("%.20La", L"%.20La");
}
#endif

TEST(LlvmLibcWidePrintfConverterTest, DoubleBoundaryAgreement) {
  check_float_boundaries<double>("%.20e", L"%.20e");
  check_float_boundaries<double>("%.20g", L"%.20g");
  check_float_boundaries<double>("%.20a", L"%.20a");
}

TEST(LlvmLibcWidePrintfConverterTest, FloatTruncation) {
  wchar_t buffer[5];
  EXPECT_EQ(format_to(buffer, 5, L"%.100f", 1.25), BUFFER_TOO_SMALL);
  EXPECT_TRUE(basic_string_view<wchar_t>(buffer) == L"1.25");
#ifndef LIBC_COPT_PRINTF_DISABLE_WRITE_INT
  int count = -1;
  EXPECT_EQ(format_to(buffer, 5, L"%100f%n", 1.25, &count), BUFFER_TOO_SMALL);
  EXPECT_EQ(count, -1);
  check(L"\u4e2d1.25", L"\u4e2d%.2f%n", 1.25, &count);
  EXPECT_EQ(count, 5);
#endif
}
#endif

TEST(LlvmLibcWidePrintfConverterTest, Pointers) {
  check(L"0x123", L"%p", reinterpret_cast<void *>(uintptr_t(0x123)));
  check(L"   (nullptr)", L"%12p", static_cast<void *>(nullptr));
  check(L"(nullptr)   ", L"%-12p", static_cast<void *>(nullptr));
  check(L"(nu", L"%.3p", static_cast<void *>(nullptr));
}

#ifndef LIBC_COPT_PRINTF_DISABLE_WRITE_INT
TEST(LlvmLibcWidePrintfConverterTest, WideCounts) {
  signed char hh = -1;
  short h = -1;
  int n = -1;
  long l = -1;
  long long ll = -1;
  intmax_t j = -1;
  using SignedSize = LIBC_NAMESPACE::cpp::make_signed_t<size_t>;
  SignedSize z = -1;
  ptrdiff_t t = -1;
  check(L"\u4e2d%12", L"\u4e2d%%12%hhn%hn%n%ln%lln%jn%zn%tn", &hh, &h, &n, &l,
        &ll, &j, &z, &t);
  EXPECT_EQ(hh, static_cast<signed char>(4));
  EXPECT_EQ(h, short(4));
  EXPECT_EQ(n, 4);
  EXPECT_EQ(l, 4L);
  EXPECT_EQ(ll, 4LL);
  EXPECT_EQ(j, intmax_t(4));
  EXPECT_EQ(z, SignedSize(4));
  EXPECT_EQ(t, ptrdiff_t(4));
}

TEST(LlvmLibcWidePrintfConverterTest, TruncationStopsBeforeCountStore) {
  wchar_t buffer[4];
  int count = -1;
  EXPECT_EQ(format_to(buffer, 4, L"%100d%n", 7, &count), BUFFER_TOO_SMALL);
  EXPECT_EQ(count, -1);
  EXPECT_TRUE(basic_string_view<wchar_t>(buffer) == L"   ");

  WideWriter writer(buffer, 1);
  ASSERT_EQ(writer.write(L'x'), BUFFER_TOO_SMALL);
  BasicFormatSection<wchar_t> section{};
  section.has_conv = true;
  section.conv_name = 'n';
  section.conv_val_ptr = &count;
  EXPECT_EQ(convert_wide(&writer, section), BUFFER_TOO_SMALL);
  EXPECT_EQ(count, -1);
}
#endif

#ifndef LIBC_COPT_PRINTF_DISABLE_INDEX_MODE
TEST(LlvmLibcWidePrintfConverterTest, IndexedIntegers) {
  check(L"  002a 6", L"%3$*1$.*2$x %1$d", 6, 4, 42U);
}
#endif
