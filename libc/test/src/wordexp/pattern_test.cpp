//===-- Parameter pattern removal tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/wordexp/pattern.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::CharVector;
using LIBC_NAMESPACE::cpp::string_view;
using Op = ParameterExpansion::Operation;

namespace {
void check(string_view pattern, string_view value, Op op, const char *expected,
           size_t removed, bool utf8 = false) {
  CharVector result;
  auto count = remove_word_pattern(
      pattern, value, op, RestartableConverter(utf8), nullptr, nullptr, result);
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, removed);
  EXPECT_STREQ(result.c_str(), expected);
}
} // namespace

TEST(LlvmLibcWordPatternTest, FourOperatorsAndEmptyMatches) {
  check("a*", "abac", Op::PREFIX, "bac", 1);
  check("a*", "abac", Op::LONGEST_PREFIX, "", 4);
  check("*c", "abac", Op::SUFFIX, "aba", 1);
  check("*c", "abac", Op::LONGEST_SUFFIX, "", 4);
  check("*", "value", Op::PREFIX, "value", 0);
  check("*", "value", Op::SUFFIX, "value", 0);
  check("*", "", Op::LONGEST_PREFIX, "", 0);
  check("z", "abc", Op::LONGEST_SUFFIX, "abc", 0);
}

TEST(LlvmLibcWordPatternTest, PatternQuotingAndNoPathnameRestrictions) {
  check("\\*", "*value", Op::PREFIX, "value", 1);
  check("\\?", "value?", Op::SUFFIX, "value", 1);
  check("?", ".value", Op::PREFIX, "value", 1);
  check("*b", "a/b/c", Op::PREFIX, "/c", 3);
  check("[[:digit:]]*", "123abc", Op::PREFIX, "23abc", 1);
  check("[!a]", "bac", Op::PREFIX, "ac", 1);
}

TEST(LlvmLibcWordPatternTest, CharactersNotUtf8Bytes) {
  check("?", "\xe4\xb8\xad\xc4\xb1x", Op::PREFIX, "\xc4\xb1x", 1, true);
  check("??", "\xe4\xb8\xad\xc4\xb1x", Op::SUFFIX, "\xe4\xb8\xad", 2, true);
  auto count = word_parameter_length("\xe4\xb8\xad\xc4\xb1x",
                                     RestartableConverter(true));
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, size_t(3));
  count = word_parameter_length("\xff", RestartableConverter(false));
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, size_t(1));
}

TEST(LlvmLibcWordPatternTest, InvalidInputDoesNotPublish) {
  CharVector result;
  ASSERT_TRUE(result.append('x'));
  const char *bad[] = {"\xc4", "\xed\xa0\x80", "\xff"};
  for (auto value : bad) {
    auto count =
        remove_word_pattern("*", value, Op::PREFIX, RestartableConverter(true),
                            nullptr, nullptr, result);
    ASSERT_FALSE(count.has_value());
    EXPECT_EQ(count.error(), EILSEQ);
    EXPECT_STREQ(result.c_str(), "x");
  }
  auto count =
      remove_word_pattern("*", "value", Op::VALUE, RestartableConverter(false),
                          nullptr, nullptr, result);
  ASSERT_FALSE(count.has_value());
  EXPECT_EQ(count.error(), EINVAL);
  count = word_parameter_length({"a\0b", 3}, RestartableConverter(false));
  ASSERT_FALSE(count.has_value());
  EXPECT_EQ(count.error(), EILSEQ);
}
