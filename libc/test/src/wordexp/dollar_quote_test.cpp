//===-- Dollar-single-quote decoding tests --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/wordexp/dollar_quote.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using Kind = QuotedUnit::Kind;
using Error = WordSyntaxError;

namespace {
struct Output {
  uint32_t input[256]{};
  QuotedUnit units[256]{};
  size_t count = 0, fail_at = 256;
  int failure = ENOMEM;
  static int emit(void *opaque, QuotedUnit unit) {
    auto &self = *static_cast<Output *>(opaque);
    if (self.count == self.fail_at)
      return self.failure;
    self.units[self.count++] = unit;
    return 0;
  }
  WordSyntaxResult run(const char *s) {
    count = 0;
    size_t size = 0;
    while (*s)
      input[size++] = static_cast<unsigned char>(*s++);
    return decode_dollar_quote({input, size}, {this, emit});
  }
};
} // namespace

TEST(LlvmLibcDollarQuoteTest, CharactersAndNumericBytesStayDistinct) {
  Output out;
  ASSERT_TRUE(bool(out.run("\\a\\b\\e\\f\\n\\r\\t\\v\\\"\\'\\\\")));
  constexpr uint32_t expected[] = {7, 8,  27,  12,   10,  13,
                                   9, 11, '"', '\'', '\\'};
  ASSERT_EQ(out.count, sizeof(expected) / sizeof(*expected));
  for (size_t i = 0; i < out.count; ++i) {
    EXPECT_TRUE(out.units[i].kind == Kind::CHARACTER);
    EXPECT_EQ(out.units[i].value, expected[i]);
  }
  ASSERT_TRUE(bool(out.run("\\x1b\\33\\377\\1234\\xAq")));
  constexpr uint32_t bytes[] = {27, 27, 255, 83, '4', 10, 'q'};
  ASSERT_EQ(out.count, sizeof(bytes) / sizeof(*bytes));
  for (size_t i = 0; i < out.count; ++i) {
    EXPECT_EQ(out.units[i].value, bytes[i]);
    EXPECT_TRUE(out.units[i].kind ==
                ((i == 4 || i == 6) ? Kind::CHARACTER : Kind::BYTE));
  }
}

TEST(LlvmLibcDollarQuoteTest, ControlCharacterTable) {
  Output out;
  for (char c = 'a'; c <= 'z'; ++c) {
    char input[] = {'\\', 'c', c, 0};
    ASSERT_TRUE(bool(out.run(input)));
    EXPECT_EQ(out.count, size_t(1));
    EXPECT_EQ(out.units[0].value, uint32_t(c - 'a' + 1));
    input[2] = char(c - 'a' + 'A');
    ASSERT_TRUE(bool(out.run(input)));
    EXPECT_EQ(out.units[0].value, uint32_t(c - 'a' + 1));
  }
  ASSERT_TRUE(bool(out.run("\\c[\\c\\\\\\c]\\c^\\c_\\c?")));
  constexpr uint32_t expected[] = {27, 28, 29, 30, 31, 127};
  ASSERT_EQ(out.count, size_t(6));
  for (size_t i = 0; i < out.count; ++i) {
    EXPECT_EQ(out.units[i].value, expected[i]);
    EXPECT_TRUE(out.units[i].kind == Kind::CHARACTER);
  }
}

TEST(LlvmLibcDollarQuoteTest, RetainsLiteralAndEscapeSourceOffsets) {
  Output out;
  ASSERT_TRUE(bool(out.run("a\\n\\x41z\\0discarded")));
  ASSERT_EQ(out.count, size_t(4));
  const size_t begins[] = {0, 1, 3, 7}, ends[] = {1, 3, 7, 8};
  for (size_t i = 0; i < out.count; ++i) {
    EXPECT_EQ(out.units[i].begin, begins[i]);
    EXPECT_EQ(out.units[i].end, ends[i]);
    EXPECT_EQ(out.units[i].literal, i == 0 || i == 3);
  }
}

TEST(LlvmLibcDollarQuoteTest, ErrorsPrecedeSinkEffects) {
  Output out;
  const char *invalid[] = {"a\\",    "a'",    "a\\q",  "\\\n",  "\\x",
                           "\\x123", "\\400", "\\777", "\\c",   "\\c\\",
                           "\\c\\x", "\\c@",  "\\c0",  "\\0\\q"};
  for (auto s : invalid) {
    EXPECT_TRUE(out.run(s).error == Error::SYNTAX);
    EXPECT_EQ(out.count, size_t(0));
  }
  uint32_t invalid_scalar[] = {'a', 0xd800};
  EXPECT_TRUE(decode_dollar_quote(invalid_scalar, {&out, Output::emit}).error ==
              Error::SYNTAX);
  EXPECT_EQ(out.count, size_t(0));
}

TEST(LlvmLibcDollarQuoteTest, NulDiscardsOnlyThisSpan) {
  Output out;
  ASSERT_TRUE(bool(out.run("prefix\\0discard\\n\\x41")));
  EXPECT_EQ(out.count, size_t(6));
  EXPECT_FALSE(bool(out.run("\\x00discard"))); // More than two hex digits.
  ASSERT_TRUE(bool(out.run("\\x00!discard")));
  EXPECT_EQ(out.count, size_t(0));
  uint32_t next[] = {'a', 0x4e2d};
  ASSERT_TRUE(bool(decode_dollar_quote(next, {&out, Output::emit})));
  EXPECT_EQ(out.count, size_t(2));
  EXPECT_EQ(out.units[1].value, uint32_t(0x4e2d));
}

TEST(LlvmLibcDollarQuoteTest, SinkErrorsArePreserved) {
  Output out;
  for (size_t fail = 0; fail != 4; ++fail) {
    out.fail_at = fail;
    auto result = out.run("a\\n\\xffz");
    EXPECT_TRUE(result.error == Error::SINK_ERROR);
    EXPECT_EQ(result.sink_error, ENOMEM);
    EXPECT_EQ(out.count, fail);
  }
  out.fail_at = 1;
  out.failure = EILSEQ;
  auto error = out.run("a\\e");
  EXPECT_TRUE(error.error == Error::SINK_ERROR);
  EXPECT_EQ(error.position, size_t(1));
  EXPECT_EQ(error.sink_error, EILSEQ);
}
