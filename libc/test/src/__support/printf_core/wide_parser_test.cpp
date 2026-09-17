//===-- Unittests for wide printf parsing
//----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/wint_t.h"
#include "src/__support/CPP/bit.h"
#include "src/__support/arg_list.h"
#include "src/__support/printf_core/parser.h"
#include "test/UnitTest/Test.h"

#include <stdarg.h>

using LIBC_NAMESPACE::internal::ArgList;
using namespace LIBC_NAMESPACE::printf_core;
using WideSection = BasicFormatSection<wchar_t>;

namespace {

// Include the terminal empty section so the tests also check parser progress.
template <size_t N>
void evaluate(WideSection (&sections)[N], const wchar_t *format, ...) {
  va_list vlist;
  va_start(vlist, format);
  ArgList args(vlist);
  va_end(vlist);
  Parser<ArgList, wchar_t> parser(format, args);
  for (auto &section : sections)
    section = parser.get_next_section();
  EXPECT_TRUE(sections[N - 1].raw_string.empty());
}

void expect_raw(const WideSection &section, const wchar_t *data, size_t size,
                bool has_conv) {
  EXPECT_EQ(section.raw_string.data(), data);
  EXPECT_EQ(section.raw_string.size(), size);
  EXPECT_EQ(section.has_conv, has_conv);
}

} // namespace

TEST(LlvmLibcWidePrintfParserTest, Empty) {
  WideSection sections[1];
  evaluate(sections, L"");
  EXPECT_FALSE(sections[0].has_conv);
}

TEST(LlvmLibcWidePrintfParserTest, LiteralsAndPercent) {
  const wchar_t *format = L"\u03bb\u4e2d%%\u00e9";
  WideSection sections[4];
  evaluate(sections, format);
  expect_raw(sections[0], format, 2, false);
  expect_raw(sections[1], format + 2, 2, true);
  EXPECT_EQ(sections[1].conv_name, '%');
  expect_raw(sections[2], format + 4, 1, false);
}

TEST(LlvmLibcWidePrintfParserTest, FlagsAndLiteralWidthPrecision) {
  const wchar_t *format = L"%+-0 #12.7lld";
  WideSection sections[2];
  evaluate(sections, format, 123LL);
  expect_raw(sections[0], format, 13, true);
  EXPECT_EQ(static_cast<unsigned>(sections[0].flags),
            static_cast<unsigned>(LEFT_JUSTIFIED | FORCE_SIGN | SPACE_PREFIX |
                                  ALTERNATE_FORM | LEADING_ZEROES));
  EXPECT_EQ(sections[0].min_width, 12);
  EXPECT_EQ(sections[0].precision, 7);
  EXPECT_TRUE(sections[0].length_modifier == LengthModifier::ll);
  EXPECT_EQ(sections[0].conv_val_raw, AnyFloatStorageType(123));
}

TEST(LlvmLibcWidePrintfParserTest, DynamicWidthPrecisionAndArguments) {
  WideSection sections[3];
  evaluate(sections, L"%*.*d%d", -12, -3, 42, 7);
  EXPECT_EQ(sections[0].min_width, 12);
  EXPECT_EQ(sections[0].precision, -3);
  EXPECT_TRUE((sections[0].flags & LEFT_JUSTIFIED) != 0);
  EXPECT_EQ(sections[0].conv_val_raw, AnyFloatStorageType(42));
  EXPECT_EQ(sections[1].conv_val_raw, AnyFloatStorageType(7));
}

TEST(LlvmLibcWidePrintfParserTest, IntegerLengthModifiers) {
  WideSection sections[9];
  evaluate(sections, L"%hhd%hd%d%ld%lld%jd%zu%td", 1, 2, 3, 4L, 5LL,
           intmax_t(6), size_t(7), ptrdiff_t(8));
  const LengthModifier modifiers[] = {LengthModifier::hh,   LengthModifier::h,
                                      LengthModifier::none, LengthModifier::l,
                                      LengthModifier::ll,   LengthModifier::j,
                                      LengthModifier::z,    LengthModifier::t};
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_TRUE(sections[i].has_conv);
    EXPECT_TRUE(sections[i].length_modifier == modifiers[i]);
    EXPECT_EQ(sections[i].conv_val_raw, AnyFloatStorageType(i + 1));
  }
}

TEST(LlvmLibcWidePrintfParserTest, CharactersAndPointers) {
  WideSection sections[6];
  char narrow[] = "abc";
  wchar_t wide[] = L"\u4e2d";
  evaluate(sections, L"%c%lc%s%ls%p", int('x'), wint_t(L'\u03bb'), narrow, wide,
           static_cast<void *>(wide));
  EXPECT_EQ(sections[0].conv_val_raw, AnyFloatStorageType('x'));
  EXPECT_EQ(sections[1].conv_val_raw, AnyFloatStorageType(L'\u03bb'));
  EXPECT_TRUE(sections[1].length_modifier == LengthModifier::l);
  EXPECT_EQ(sections[2].conv_val_ptr, static_cast<void *>(narrow));
  EXPECT_EQ(sections[3].conv_val_ptr, static_cast<void *>(wide));
  EXPECT_TRUE(sections[3].length_modifier == LengthModifier::l);
  EXPECT_EQ(sections[4].conv_val_ptr, static_cast<void *>(wide));
}

#ifndef LIBC_COPT_PRINTF_DISABLE_WRITE_INT
TEST(LlvmLibcWidePrintfParserTest, CountPointer) {
  WideSection sections[3];
  int count = -1;
  evaluate(sections, L"\u4e2d%n", &count);
  EXPECT_TRUE(sections[1].has_conv);
  EXPECT_EQ(sections[1].conv_name, 'n');
  EXPECT_EQ(sections[1].conv_val_ptr, static_cast<void *>(&count));
  EXPECT_EQ(count, -1); // Parsing must not perform the conversion.
}
#endif

#ifndef LIBC_COPT_PRINTF_DISABLE_FLOAT
TEST(LlvmLibcWidePrintfParserTest, FloatingArguments) {
  WideSection sections[3];
  evaluate(sections, L"%f%d", 1.5, 7);
  EXPECT_EQ(sections[0].conv_val_raw,
            AnyFloatStorageType(LIBC_NAMESPACE::cpp::bit_cast<uint64_t>(1.5)));
  EXPECT_EQ(sections[1].conv_val_raw, AnyFloatStorageType(7));
}

#ifndef LIBC_TYPES_LONG_DOUBLE_IS_DOUBLE_DOUBLE
TEST(LlvmLibcWidePrintfParserTest, LongDouble) {
  WideSection sections[2];
  evaluate(sections, L"%.3Lf", 1.5L);
  EXPECT_TRUE(sections[0].length_modifier == LengthModifier::L);
  EXPECT_EQ(sections[0].precision, 3);
  using Bits = LIBC_NAMESPACE::fputil::FPBits<long double>;
  // Ignore storage padding, for example the unused bits of x87 long double.
  EXPECT_EQ(
      Bits(static_cast<Bits::StorageType>(sections[0].conv_val_raw)).uintval(),
      Bits(1.5L).uintval());
}
#endif
#endif

TEST(LlvmLibcWidePrintfParserTest, TrailingPercent) {
  const wchar_t *format = L"\u03bb%";
  WideSection sections[3];
  evaluate(sections, format);
  expect_raw(sections[0], format, 1, false);
  expect_raw(sections[1], format + 1, 1, false);
}

TEST(LlvmLibcWidePrintfParserTest, WideValuesAreNotNarrowedForParsing) {
  // The low bytes resemble a digit, 'd', and '%', respectively.
  const wchar_t *format = L"%\u0131%\u0164\u0125%d";
  WideSection sections[5];
  evaluate(sections, format, 42);
  expect_raw(sections[0], format, 2, false);
  expect_raw(sections[1], format + 2, 2, false);
  expect_raw(sections[2], format + 4, 1, false);
  EXPECT_TRUE(sections[3].has_conv);
  EXPECT_EQ(sections[3].conv_val_raw, AnyFloatStorageType(42));
}

TEST(LlvmLibcWidePrintfParserTest, NumericFieldOverflow) {
  WideSection sections[2];
  evaluate(sections, L"%99999999999999999999.99999999999999999999d", 1);
  EXPECT_TRUE(sections[0].has_conv);
  EXPECT_EQ(sections[0].min_width, INT_MAX);
  EXPECT_EQ(sections[0].precision, INT_MAX);
}

#ifndef LIBC_COPT_PRINTF_DISABLE_BITINT
TEST(LlvmLibcWidePrintfParserTest, BitWidthModifier) {
  WideSection sections[3];
  evaluate(sections, L"%w8d%wf16d", 1, 2);
  EXPECT_TRUE(sections[0].length_modifier == LengthModifier::w);
  EXPECT_EQ(sections[0].bit_width, size_t(8));
  EXPECT_TRUE(sections[1].length_modifier == LengthModifier::wf);
  EXPECT_EQ(sections[1].bit_width, size_t(16));
  EXPECT_EQ(sections[1].conv_val_raw, AnyFloatStorageType(2));
}
#endif

#ifndef LIBC_COPT_PRINTF_DISABLE_INDEX_MODE
TEST(LlvmLibcWidePrintfParserTest, IndexedWidthPrecisionAndRewind) {
  WideSection sections[5];
  evaluate(sections, L"%3$*1$.*2$d%1$d%2$d%3$d", 12, 4, 42);
  EXPECT_EQ(sections[0].min_width, 12);
  EXPECT_EQ(sections[0].precision, 4);
  EXPECT_EQ(sections[0].conv_val_raw, AnyFloatStorageType(42));
  EXPECT_EQ(sections[1].conv_val_raw, AnyFloatStorageType(12));
  EXPECT_EQ(sections[2].conv_val_raw, AnyFloatStorageType(4));
  EXPECT_EQ(sections[3].conv_val_raw, AnyFloatStorageType(42));
}

#ifndef LIBC_COPT_PRINTF_DISABLE_FLOAT
TEST(LlvmLibcWidePrintfParserTest, IndexedMixedTypes) {
  WideSection sections[4];
  wchar_t text[] = L"\u03bb";
  evaluate(sections, L"%3$ls%2$f%1$d", 7, 1.5, text);
  EXPECT_EQ(sections[0].conv_val_ptr, static_cast<void *>(text));
  EXPECT_EQ(sections[1].conv_val_raw,
            AnyFloatStorageType(LIBC_NAMESPACE::cpp::bit_cast<uint64_t>(1.5)));
  EXPECT_EQ(sections[2].conv_val_raw, AnyFloatStorageType(7));
}
#endif

TEST(LlvmLibcWidePrintfParserTest, MissingIndex) {
  WideSection sections[2];
  evaluate(sections, L"%2$d", 1, 2);
  EXPECT_FALSE(sections[0].has_conv);
}
#else
TEST(LlvmLibcWidePrintfParserTest, IndexModeDisabled) {
  const wchar_t *format = L"%1$d";
  WideSection sections[3];
  evaluate(sections, format);
  expect_raw(sections[0], format, 3, false);
  expect_raw(sections[1], format + 3, 1, false);
}
#endif
