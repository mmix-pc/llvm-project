//===-- Locale-aware numeric entry point tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/__support/printf_core/converter.h"
#include "src/__support/printf_core/wide_writer.h"
#include "src/locale/locale.h"
#include "src/locale/numeric_conversion.h"
#include "src/locale/uselocale.h"
#include "src/stdio/scanf_core/float_converter.h"
#include "src/stdio/snprintf.h"
#include "src/stdio/sscanf.h"
#include "src/stdlib/strtod.h"
#include "src/stdlib/strtod_l.h"
#include "src/stdlib/strtof.h"
#include "src/stdlib/strtol.h"
#include "src/stdlib/strtol_l.h"
#include "src/wchar/swprintf.h"
#include "src/wchar/swscanf.h"
#include "src/wchar/wcstod.h"
#include "src/wchar/wcstol.h"
#include "test/UnitTest/Test.h"
#include "test/src/__support/wchar/ISO2022CNFixture.h"

using namespace LIBC_NAMESPACE::internal;

namespace {
struct Selection {
  __locale_data numeric{"test", true};
  __locale_t locale = locale_from_mask(1U << LC_CTYPE);
  locale_t previous;
  explicit Selection(const char *radix, const char *thousands = "",
                     LIBC_NAMESPACE::cpp::span<const uint8_t> grouping = {}) {
    size_t length = 0;
    while (radix[length])
      ++length;
    NumericLocaleData data{};
    data.decimal_point = {
        LocaleDataView::Kind::STRING,
        {reinterpret_cast<const uint8_t *>(radix), length + 1}};
    length = 0;
    while (thousands[length])
      ++length;
    data.thousands_sep = {
        LocaleDataView::Kind::STRING,
        {reinterpret_cast<const uint8_t *>(thousands), length + 1}};
    data.grouping = {LocaleDataView::Kind::BYTES, grouping};
    numeric.numeric = LIBC_NAMESPACE::cpp::optional<NumericLocaleData>(data);
    locale.data[LC_NUMERIC] = &numeric;
    previous = LIBC_NAMESPACE::uselocale(&locale);
  }
  ~Selection() { LIBC_NAMESPACE::uselocale(previous); }
};
} // namespace

TEST(LlvmLibcNumericLocaleTest, StatefulLiteralFormatDoesNotConsumeArguments) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  // A valid two-byte character contains the ASCII bytes for "%s".
  fixture.put(fixture.bytes + 226, 0x2572);
  fixture.put(fixture.bytes + 234, 0x2573);
  fixture.put(fixture.bytes + 246, 0x2572);
  fixture.put(fixture.bytes + 254, 0x2573);
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  __locale_data category{"stateful-format", false};
  category.chinese_encoding = *mapping;
  auto locale = locale_from_mask(0);
  locale.data[LC_CTYPE] = &category;
  auto previous = LIBC_NAMESPACE::uselocale(&locale);
  const char format[] = "\x1b$)A\x0e%s\x0f";
  char output[32];
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), format, "BAD"),
            int(sizeof(format) - 1));
  EXPECT_STREQ(output, format);
  char destination[32] = "unchanged";
  EXPECT_EQ(LIBC_NAMESPACE::sscanf(format, format, destination), 0);
  EXPECT_STREQ(destination, "unchanged");
  const char with_conversion[] = "\x1b$)A\x0e%s\x0f%d";
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), with_conversion, 7),
            9);
  EXPECT_STREQ(output, "\x1b$)A\x0e%s\x0f" "7");
  int number = -1;
  EXPECT_EQ(LIBC_NAMESPACE::sscanf(output, with_conversion, &number), 1);
  EXPECT_EQ(number, 7);
  LIBC_NAMESPACE::uselocale(previous);
}

TEST(LlvmLibcNumericLocaleTest, StatefulFormatArgumentPreScan) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  fixture.put(fixture.bytes + 226, 0x2466);
  fixture.put(fixture.bytes + 234, 0x2531);
  fixture.put(fixture.bytes + 246, 0x2466);
  fixture.put(fixture.bytes + 254, 0x2531);
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  __locale_data category{"stateful-format", false};
  category.chinese_encoding = *mapping;
  auto locale = locale_from_mask(0);
  locale.data[LC_CTYPE] = &category;
  auto previous = LIBC_NAMESPACE::uselocale(&locale);
  // The apparent %1$f occurs inside two encoded characters, not an argument.
  // Reading argument 2 first requires finding the real type of argument 1.
  const char format[] = "%2$d\x1b$)A\x0e%1$f\x0f%1$d";
  char output[32];
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), format, 3, 7), 12);
  EXPECT_STREQ(output, "7\x1b$)A\x0e%1$f\x0f" "3");
  LIBC_NAMESPACE::uselocale(previous);
}

TEST(LlvmLibcNumericLocaleTest, RadixAndEndPointer) {
  Selection selected(",");
  const char *input = " \t-12,5e1tail";
  char *end = nullptr;
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_TRUE(LIBC_NAMESPACE::strtod(input, &end) == -125.0);
  EXPECT_STREQ(end, "tail");
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_TRUE(LIBC_NAMESPACE::strtod("0x1,8p2!", &end) == 6.0);
  EXPECT_STREQ(end, "!");
  EXPECT_TRUE(LIBC_NAMESPACE::strtod("1.5", &end) == 1.0);
  EXPECT_STREQ(end, ".5");
  EXPECT_TRUE(LIBC_NAMESPACE::strtod("1.000,5", &end) == 1.0);
  EXPECT_STREQ(end, ".000,5");
  input = " \t-,e2";
  EXPECT_TRUE(LIBC_NAMESPACE::strtod(input, &end) == 0.0);
  EXPECT_TRUE(end == input);
}

TEST(LlvmLibcNumericLocaleTest, ExplicitSelectionDoesNotChangeThread) {
  Selection selected(",");
  auto c = locale_from_mask(0);
  char *end = nullptr;
  EXPECT_TRUE(LIBC_NAMESPACE::strtod_l("12.5!", &end, &c) == 12.5);
  EXPECT_STREQ(end, "!");
  EXPECT_TRUE(LIBC_NAMESPACE::strtod("12,5", nullptr) == 12.5);
  EXPECT_TRUE(LIBC_NAMESPACE::uselocale(nullptr) == &selected.locale);
  LIBC_NAMESPACE::uselocale(&c);
  EXPECT_TRUE(LIBC_NAMESPACE::strtod_l("12,5", nullptr, &selected.locale) ==
              12.5);
  EXPECT_TRUE(LIBC_NAMESPACE::uselocale(nullptr) == &c);
}

TEST(LlvmLibcNumericLocaleTest, WideRadixAndCategoryWhitespace) {
  Selection selected("\xd9\xab");
  char *end = nullptr;
  EXPECT_TRUE(LIBC_NAMESPACE::strtod("2\xd9\xab"
                                     "5!",
                                     &end) == 2.5);
  EXPECT_STREQ(end, "!");
  const char *partial = "2\xd9";
  EXPECT_TRUE(LIBC_NAMESPACE::strtod(partial, &end) == 2.0);
  EXPECT_TRUE(end == partial + 1);
  wchar_t *wend = nullptr;
  EXPECT_TRUE(LIBC_NAMESPACE::wcstod(L"\u2003-2\u066b5!", &wend) == -2.5);
  EXPECT_EQ(*wend, L'!');
  EXPECT_EQ(LIBC_NAMESPACE::wcstol(L"\u2003-42!", &wend, 10), -42L);
  EXPECT_EQ(*wend, L'!');
  selected.locale.data[LC_CTYPE] = locale_from_mask(0).data[LC_CTYPE];
  const wchar_t *wide = L"\u2003-2\u066b5";
  EXPECT_TRUE(LIBC_NAMESPACE::wcstod(wide, &wend) == 0.0);
  EXPECT_TRUE(wend == wide);
  EXPECT_EQ(LIBC_NAMESPACE::wcstol(wide, &wend, 10), 0L);
  EXPECT_TRUE(wend == wide);
}

TEST(LlvmLibcNumericLocaleTest, RangeAndIntegerGrammar) {
  Selection selected(",");
  char *end = nullptr;
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_TRUE(LIBC_NAMESPACE::strtof("1,25e99999!", &end) > 1.0f);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  EXPECT_STREQ(end, "!");
  EXPECT_EQ(LIBC_NAMESPACE::strtol(" \t0x2a!", &end, 0), 42L);
  EXPECT_STREQ(end, "!");
  // Retain the separately recorded binary-prefix compatibility behavior.
  EXPECT_EQ(LIBC_NAMESPACE::strtol("0b101!", &end, 0), 5L);
  EXPECT_STREQ(end, "!");
  char sentinel;
  end = &sentinel;
  EXPECT_EQ(LIBC_NAMESPACE::strtol_l("123", &end, 37, &selected.locale), 0L);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(end, &sentinel);
}

TEST(LlvmLibcNumericLocaleTest, InternalParsersRemainLocaleIndependent) {
  Selection selected(",");
  auto number = strtofloatingpoint<double>("1.5!");
  EXPECT_TRUE(number.value == 1.5);
  EXPECT_EQ(number.parsed_len, ptrdiff_t(3));
  number = strtofloatingpoint<double>("1,5!");
  EXPECT_TRUE(number.value == 1.0);
  EXPECT_EQ(number.parsed_len, ptrdiff_t(1));
}

TEST(LlvmLibcNumericLocaleTest, ScannersKeepOriginalProgressAndFieldWidth) {
  Selection selected("\xd9\xab");
  double result = 99.0;
  int count = -1;
  char next = 0;
  EXPECT_EQ(LIBC_NAMESPACE::sscanf("2\xd9\xab"
                                   "5!",
                                   "%lf%n%c", &result, &count, &next),
            2);
  EXPECT_TRUE(result == 2.5);
  EXPECT_EQ(count, 4);
  EXPECT_EQ(next, '!');
  result = 99.0;
  EXPECT_EQ(LIBC_NAMESPACE::sscanf("2\xd9\xab"
                                   "5!",
                                   "%2lf", &result),
            0);
  EXPECT_TRUE(result == 99.0);
  EXPECT_EQ(LIBC_NAMESPACE::sscanf("2\xd9!", "%lf", &result), 0);
  EXPECT_TRUE(result == 99.0);
  EXPECT_EQ(LIBC_NAMESPACE::sscanf("0x1\xd9\xab"
                                   "8p2!",
                                   "%lf%n", &result, &count),
            1);
  EXPECT_TRUE(result == 6.0);
  EXPECT_EQ(count, 8);
  EXPECT_EQ(LIBC_NAMESPACE::swscanf(L"\u2003"
                                    L"2\u066b5!",
                                    L"%lf%n", &result, &count),
            1);
  EXPECT_TRUE(result == 2.5);
  EXPECT_EQ(count, 4);
  selected.locale.data[LC_CTYPE] = locale_from_mask(0).data[LC_CTYPE];
  result = 99.0;
  EXPECT_EQ(LIBC_NAMESPACE::swscanf(L"\u2003"
                                    L"2\u066b5!",
                                    L"%lf", &result),
            0);
  EXPECT_TRUE(result == 99.0);
}

TEST(LlvmLibcNumericLocaleTest, PartialRadixRetainsUnmatchedInput) {
  Selection selected("\xd9\xab");
  namespace sc = LIBC_NAMESPACE::scanf_core;
  class Input : public sc::Reader<Input> {
    const char *text;
    size_t position = 0;

  public:
    size_t reads = 0;
    explicit Input(const char *text) : text(text) {}
    int getc() {
      ++reads;
      return text[position] ? static_cast<unsigned char>(text[position++]) : -1;
    }
    void ungetc(int value) {
      if (value >= 0)
        --position;
    }
  };
  for (unsigned bounded = 0; bounded != 2; ++bounded) {
    Input input(bounded ? "2\xd9\xab!" : "2\xd9!");
    auto *reader = static_cast<sc::Reader<Input> *>(&input);
    double output = 99.0;
    sc::FormatSection section;
    section.conv_name = 'f';
    section.length_modifier = sc::LengthModifier::l;
    section.output_ptr = &output;
    section.max_width = bounded ? 2 : 0;
    EXPECT_EQ(sc::convert_float(reader, section), int(sc::MATCHING_FAILURE));
    EXPECT_TRUE(output == 99.0);
    EXPECT_EQ(reader->chars_read(), size_t(2));
    EXPECT_EQ(input.reads, bounded ? size_t(2) : size_t(3));
    EXPECT_EQ(reader->getc(), bounded ? 0xab : int('!'));
  }
}

TEST(LlvmLibcNumericLocaleTest, FormattingCountsBytesAndWideCharacters) {
  Selection selected("\xd9\xab");
  char output[64];
  wchar_t wide[64];
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%8.1f", 1.5), 8);
  EXPECT_STREQ(output, "    1\xd9\xab"
                       "5");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%-8.1f", 1.5), 8);
  EXPECT_STREQ(output, "1\xd9\xab"
                       "5    ");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%+08.1f", 1.5),
            8);
  EXPECT_STREQ(output, "+0001\xd9\xab"
                       "5");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%8.1e", 1.5), 8);
  EXPECT_STREQ(output, "1\xd9\xab"
                       "5e+00");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%10.1a", 1.5),
            10);
  EXPECT_STREQ(output, " 0x1\xd9\xab"
                       "8p+0");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%#.0f", 1.0), 3);
  EXPECT_STREQ(output, "1\xd9\xab");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.1f", 9.96875),
            5);
  EXPECT_STREQ(output, "10\xd9\xab"
                       "0");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.1e", 9.96875),
            8);
  EXPECT_STREQ(output, "1\xd9\xab"
                       "0e+01");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.2g", 1.5), 4);
  EXPECT_STREQ(output, "1\xd9\xab"
                       "5");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.2g", 1.0), 1);
  EXPECT_STREQ(output, "1");
  EXPECT_EQ(LIBC_NAMESPACE::swprintf(wide, 64, L"%8.1f", 1.5), 8);
  const wchar_t expected[] = L"     1\u066b5";
  for (size_t i = 0; i != sizeof(expected) / sizeof(expected[0]); ++i)
    EXPECT_EQ(wide[i], expected[i]);
  EXPECT_EQ(LIBC_NAMESPACE::swprintf(wide, 64, L"%.1a", 1.5), 8);
  EXPECT_EQ(wide[3], L'\u066b');
}

TEST(LlvmLibcNumericLocaleTest, SelectedFloatAlgorithmUsesLocaleRadix) {
  Selection selected("\xd9\xab");
  namespace pc = LIBC_NAMESPACE::printf_core;
  pc::FormatSection section;
  section.has_conv = true;
  section.conv_name = 'f';
  section.precision = 1;
  section.min_width = 8;
  section.conv_val_raw = LIBC_NAMESPACE::fputil::FPBits<double>(1.5).uintval();
  char output[32];
  pc::DropOverflowBuffer buffer(output, sizeof(output) - 1);
  pc::Writer<pc::Mode<pc::WriteMode::FILL_BUFF_AND_DROP_OVERFLOW>::value>
      writer(buffer);
  EXPECT_EQ(pc::convert_float(&writer, section), 0);
  output[buffer.buff_cur] = '\0';
  EXPECT_STREQ(output, "    1\xd9\xab"
                       "5");
  EXPECT_EQ(writer.get_chars_written(), size_t(8));
  wchar_t wide[32];
  pc::WideWriter wide_output(wide, 32);
  pc::WideNumericWriter wide_writer(wide_output);
  EXPECT_EQ(pc::convert_float(&wide_writer, section), 0);
  EXPECT_EQ(wide_output.get_chars_written(), size_t(8));
  EXPECT_EQ(wide[6], L'\u066b');
}

TEST(LlvmLibcNumericLocaleTest, GroupingAndPaddingAreSeparate) {
  const uint8_t pattern[] = {3, 2, 0};
  Selection selected(".", ",", pattern);
  char output[64];
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'d", 12345678),
            11);
  EXPECT_STREQ(output, "1,23,45,678");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'014d", 12345678), 14);
  EXPECT_STREQ(output, "0001,23,45,678");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'12.10d", 12345678),
      13);
  EXPECT_STREQ(output, "001,23,45,678");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%-'14d", -12345678),
      14);
  EXPECT_STREQ(output, "-1,23,45,678  ");
  const uint8_t stop[] = {3, 2, 127, 0};
  selected.numeric.numeric->grouping.bytes = stop;
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'d", 12345678),
            10);
  EXPECT_STREQ(output, "123,45,678");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'5.0u", 0U), 5);
  EXPECT_STREQ(output, "     ");
}

TEST(LlvmLibcNumericLocaleTest, GroupingSpansFloatBlocksAndRounding) {
  const uint8_t pattern[] = {3, 0};
  Selection selected(".", ",", pattern);
  char output[64];
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'20.1f",
                                     1234567890123.5),
            20);
  EXPECT_STREQ(output, " 1,234,567,890,123.5");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'010.1f", 999.96875),
      10);
  EXPECT_STREQ(output, "0001,000.0");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'12.9g", 1234567.25),
      12);
  EXPECT_STREQ(output, "1,234,567.25");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'g", 1234567.0),
            11);
  EXPECT_STREQ(output, "1.23457e+06");
}

TEST(LlvmLibcNumericLocaleTest, SelectedFloatGroupingCountsOutputUnits) {
  const uint8_t pattern[] = {3, 0};
  Selection selected("\xd9\xab", "\xe2\x80\xaf", pattern);
  namespace pc = LIBC_NAMESPACE::printf_core;
  pc::FormatSection section;
  section.has_conv = true;
  section.conv_name = 'f';
  section.precision = 1;
  section.min_width = 12;
  section.flags = pc::FormatFlags::GROUP_DIGITS;
  section.conv_val_raw =
      LIBC_NAMESPACE::fputil::FPBits<double>(1234.5).uintval();
  char output[32];
  pc::DropOverflowBuffer buffer(output, sizeof(output) - 1);
  pc::Writer<pc::Mode<pc::WriteMode::FILL_BUFF_AND_DROP_OVERFLOW>::value>
      writer(buffer);
  EXPECT_EQ(pc::convert_float(&writer, section), 0);
  output[buffer.buff_cur] = '\0';
  EXPECT_STREQ(output, "  1\xe2\x80\xaf"
                       "234\xd9\xab"
                       "5");
  EXPECT_EQ(writer.get_chars_written(), size_t(12));
  wchar_t wide[32];
  pc::WideWriter wide_output(wide, 32);
  pc::WideNumericWriter wide_writer(wide_output);
  EXPECT_EQ(pc::convert_float(&wide_writer, section), 0);
  EXPECT_EQ(wide_output.get_chars_written(), size_t(12));
  EXPECT_EQ(wide[6], L'\u202f');
  EXPECT_EQ(wide[10], L'\u066b');
}
