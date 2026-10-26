//===-- Word expansion field splitting tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/wordexp/fields.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::cpp::span;
using LIBC_NAMESPACE::cpp::string_view;
using LIBC_NAMESPACE::internal::DEFAULT_FIELD_DELIMITERS;
using LIBC_NAMESPACE::internal::ExpansionPart;
using LIBC_NAMESPACE::internal::FieldDelimiter;
using LIBC_NAMESPACE::internal::FieldSink;
using LIBC_NAMESPACE::internal::split_word_fields;

namespace {
struct Output {
  char bytes[128]{};
  bool quoted[128]{};
  size_t ends[32]{}, used = 0, count = 0, calls = 0;
  size_t fail_at = size_t(-1), empty_quoted = 0;
  FieldSink sink() { return {this, append, finish}; }
  static int append(void *data, string_view bytes, bool quoted) {
    auto &output = *static_cast<Output *>(data);
    if (output.calls++ == output.fail_at)
      return ENOMEM;
    if (bytes.size() > sizeof(output.bytes) - output.used)
      return EOVERFLOW;
    output.empty_quoted += bytes.empty() && quoted;
    for (char byte : bytes) {
      output.bytes[output.used] = byte;
      output.quoted[output.used++] = quoted;
    }
    return 0;
  }
  static int finish(void *data) {
    auto &output = *static_cast<Output *>(data);
    if (output.calls++ == output.fail_at)
      return ENOMEM;
    if (output.count == sizeof(output.ends) / sizeof(output.ends[0]))
      return EOVERFLOW;
    output.ends[output.count++] = output.used;
    return 0;
  }
  string_view field(size_t index) {
    size_t start = index ? ends[index - 1] : 0;
    return {bytes + start, ends[index] - start};
  }
};

void check(span<const ExpansionPart> parts, span<const FieldDelimiter> ifs,
           span<const string_view> expected) {
  Output output;
  auto result = split_word_fields(parts, ifs, output.sink());
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(*result, expected.size());
  ASSERT_EQ(output.count, expected.size());
  for (size_t i = 0; i != expected.size(); ++i)
    EXPECT_TRUE(output.field(i) == expected[i]);
}
} // namespace

TEST(LlvmLibcWordexpFieldsTest, DefaultAndEmptyIFS) {
  ExpansionPart parts[] = {{" \t a\n b \t", true, false}};
  string_view expected[] = {"a", "b"};
  check(parts, DEFAULT_FIELD_DELIMITERS, expected);
  string_view unsplit[] = {" \t a\n b \t"};
  check(parts, {}, unsplit);
  ExpansionPart empty[] = {{"", true, false}};
  check(empty, {}, {});
  check(empty, DEFAULT_FIELD_DELIMITERS, {});
  ExpansionPart spaces[] = {{" \t\n", true, false}};
  check(spaces, DEFAULT_FIELD_DELIMITERS, {});
  ExpansionPart literal[] = {{" a b ", false, false}};
  string_view literal_expected[] = {" a b "};
  check(literal, DEFAULT_FIELD_DELIMITERS, literal_expected);
}

TEST(LlvmLibcWordexpFieldsTest, NonWhitespaceTerminatesButDoesNotStartField) {
  FieldDelimiter ifs[] = {{" ", true}, {":", false}};
  ExpansionPart parts[] = {{" : a : : b: ", true, false}};
  string_view expected[] = {"", "a", "", "b"};
  check(parts, ifs, expected);
  parts[0].bytes = ":";
  string_view one_empty[] = {""};
  check(parts, ifs, one_empty);
  parts[0].bytes = "::";
  string_view two_empty[] = {"", ""};
  check(parts, ifs, two_empty);
  ExpansionPart adjacent[] = {
      {"a ", true, false}, {":", true, false}, {" b", true, false}};
  string_view adjacent_expected[] = {"a", "b"};
  check(adjacent, ifs, adjacent_expected);
}

TEST(LlvmLibcWordexpFieldsTest, QuotesAndLiteralOriginsSurvive) {
  ExpansionPart parts[] = {{"pre ", false, false},
                           {" a b ", true, false},
                           {" *? ", false, true},
                           {" c", true, false}};
  string_view expected[] = {"pre ", "a", "b", " *? ", "c"};
  check(parts, DEFAULT_FIELD_DELIMITERS, expected);
  ExpansionPart empty[] = {{"", false, true}};
  string_view one_empty[] = {""};
  check(empty, DEFAULT_FIELD_DELIMITERS, one_empty);
  Output output;
  auto result =
      split_word_fields(parts, DEFAULT_FIELD_DELIMITERS, output.sink());
  ASSERT_TRUE(result.has_value());
  size_t start = output.ends[2];
  for (size_t i = start; i != output.ends[3]; ++i)
    EXPECT_TRUE(output.quoted[i]);
  EXPECT_FALSE(output.quoted[0]);
  ExpansionPart trailing_quote[] = {{"a ", true, false}, {"", false, true}};
  string_view trailing_expected[] = {"a", ""};
  check(trailing_quote, DEFAULT_FIELD_DELIMITERS, trailing_expected);
}

TEST(LlvmLibcWordexpFieldsTest, MultibyteDelimitersDoNotJoinExpansionOrigins) {
  FieldDelimiter ifs[] = {{"\xc3\xa9", false}};
  ExpansionPart parts[] = {{"a\xc3\xa9"
                            "b\xc3\xa9",
                            true, false}};
  string_view expected[] = {"a", "b"};
  check(parts, ifs, expected);
  ExpansionPart separate[] = {{"a\xc3", true, false},
                              {"\xa9"
                               "b",
                               true, false}};
  string_view joined[] = {"a\xc3\xa9"
                          "b"};
  check(separate, ifs, joined);
  ExpansionPart invalid[] = {{"\xff\xc3\xa9\xfe", true, false}};
  string_view invalid_expected[] = {"\xff", "\xfe"};
  check(invalid, ifs, invalid_expected);
}

TEST(LlvmLibcWordexpFieldsTest, PropagatesSinkErrorsWithoutFurtherCalls) {
  ExpansionPart parts[] = {{"a b c", true, false}};
  Output baseline;
  ASSERT_TRUE(
      split_word_fields(parts, DEFAULT_FIELD_DELIMITERS, baseline.sink())
          .has_value());
  for (size_t i = 0; i != baseline.calls; ++i) {
    Output failed;
    failed.fail_at = i;
    EXPECT_EQ(split_word_fields(parts, DEFAULT_FIELD_DELIMITERS, failed.sink())
                  .error(),
              ENOMEM);
    EXPECT_EQ(failed.calls, i + 1);
  }
  Output invalid;
  FieldDelimiter bad[] = {{"", false}};
  EXPECT_EQ(split_word_fields(parts, bad, invalid.sink()).error(), EINVAL);
  EXPECT_EQ(invalid.calls, size_t(0));
  EXPECT_EQ(split_word_fields(parts, {}, {nullptr, nullptr, nullptr}).error(),
            EINVAL);
}
