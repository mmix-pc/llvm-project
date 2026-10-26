//===-- Word expansion lexical structure tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/wordexp/syntax.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::cpp::span;
using LIBC_NAMESPACE::internal::parse_word_syntax;
using LIBC_NAMESPACE::internal::WordSyntaxError;
using LIBC_NAMESPACE::internal::WordSyntaxResult;
using LIBC_NAMESPACE::internal::WordSyntaxSink;
using LIBC_NAMESPACE::internal::WordToken;
using Kind = WordToken::Kind;
using Error = WordSyntaxError;

namespace {
struct Tokens {
  uint32_t source[2048]{};
  size_t length = 0;
  WordToken tokens[2048]{};
  size_t count = 0;
  size_t fail_at = 2048;
  void set(const char *input) {
    length = count = 0;
    while (*input)
      source[length++] = static_cast<unsigned char>(*input++);
  }
  static int emit(void *context, WordToken token) {
    auto &self = *static_cast<Tokens *>(context);
    if (self.count == self.fail_at)
      return ENOMEM;
    self.tokens[self.count++] = token;
    return 0;
  }
  WordSyntaxResult parse(const char *input, bool nocmd = true) {
    set(input);
    return parse_word_syntax({source, length}, nocmd, {this, emit});
  }
  size_t kinds(Kind kind) const {
    size_t result = 0;
    for (size_t i = 0; i < count; ++i)
      result += tokens[i].kind == kind;
    return result;
  }
  bool text(size_t token, const char *expected) const {
    for (size_t i = tokens[token].begin; i != tokens[token].end; ++i)
      if (!*expected || source[i] != uint32_t(*expected++))
        return false;
    return !*expected;
  }
};
} // namespace

TEST(LlvmLibcWordSyntaxTest, QuotingOriginsAndEmptyWords) {
  Tokens t;
  ASSERT_TRUE(bool(t.parse("a' b'\"$x\" \\* '' \"\"")));
  EXPECT_EQ(t.kinds(Kind::END_WORD), size_t(4));
  EXPECT_TRUE(t.text(0, "a"));
  EXPECT_FALSE(t.tokens[0].quoted);
  EXPECT_TRUE(t.text(1, " b"));
  EXPECT_TRUE(t.tokens[1].quoted);
  EXPECT_TRUE(t.text(2, ""));
  EXPECT_TRUE(t.tokens[2].quoted);
  EXPECT_TRUE(t.tokens[3].kind == Kind::PARAMETER);
  EXPECT_TRUE(t.text(3, "x"));
  EXPECT_TRUE(t.tokens[3].quoted);
  EXPECT_TRUE(t.text(5, "*"));
  EXPECT_TRUE(t.tokens[5].quoted);
  ASSERT_TRUE(bool(t.parse("\\\n \\ \"a\\qb\\$c\"")));
  EXPECT_EQ(t.kinds(Kind::END_WORD), size_t(1));
  EXPECT_EQ(t.kinds(Kind::PARAMETER), size_t(0));
}

TEST(LlvmLibcWordSyntaxTest, PreflightHasNoSinkEffects) {
  Tokens t;
  const char *bad[] = {
      "valid 'unfinished", "${x=changed} a|b", "a\n", "a{b", "x ; y", "${x",
      "${#x-word}",        "${x:#a}",          "x\\"};
  for (auto input : bad) {
    EXPECT_FALSE(bool(t.parse(input)));
    EXPECT_EQ(t.count, size_t(0));
  }
  EXPECT_TRUE(t.parse("a|b").error == Error::BAD_CHARACTER);
  EXPECT_TRUE(t.parse("'x").error == Error::SYNTAX);
  t.fail_at = 2;
  auto error = t.parse("abcd");
  EXPECT_TRUE(error.error == Error::SINK_ERROR);
  EXPECT_EQ(error.sink_error, ENOMEM);
  EXPECT_EQ(t.count, size_t(2));
}

TEST(LlvmLibcWordSyntaxTest, NestedWordsAreValidatedNotEvaluated) {
  Tokens t;
  ASSERT_TRUE(bool(t.parse("${x:-\"${y:-$((1+(2)))}\"} $((1+${z:-2}))")));
  EXPECT_EQ(t.kinds(Kind::BRACED_PARAMETER), size_t(1));
  EXPECT_EQ(t.kinds(Kind::ARITHMETIC), size_t(1));
  EXPECT_TRUE(t.text(0, "x:-\"${y:-$((1+(2)))}\""));
  EXPECT_TRUE(t.text(2, "1+${z:-2}"));
  EXPECT_FALSE(bool(t.parse("${x:-${y:-'unfinished}}")));
  EXPECT_EQ(t.count, size_t(0));
  ASSERT_TRUE(bool(t.parse("\"${x-\\}}\" \"${x%'a'}\"")));
  EXPECT_EQ(t.kinds(Kind::BRACED_PARAMETER), size_t(2));
}

TEST(LlvmLibcWordSyntaxTest, ProhibitedCommandsAndQuotedSpelling) {
  Tokens t;
  const char *commands[] = {"$(echo x)",        "`echo x`",
                            "${x:-$(echo x)}",  "\"$(echo x)\"",
                            "$((1+$(echo 2)))", "$'\\''$(echo x)"};
  for (auto input : commands) {
    EXPECT_TRUE(t.parse(input).error == Error::COMMAND_FORBIDDEN);
    EXPECT_EQ(t.count, size_t(0));
  }
  ASSERT_TRUE(bool(t.parse("'$(echo x)' \\$x $'\\'$(literal)'")));
  EXPECT_EQ(t.kinds(Kind::COMMAND), size_t(0));
  EXPECT_EQ(t.kinds(Kind::DOLLAR_QUOTED), size_t(1));
  EXPECT_TRUE(t.parse("$(case x in x) echo y;; esac)", false).error ==
              Error::COMMAND_PARSER_REQUIRED);
  EXPECT_EQ(t.count, size_t(0));
}

TEST(LlvmLibcWordSyntaxTest, TildeAndLiteralDollar) {
  Tokens t;
  ASSERT_TRUE(bool(t.parse("~ ~user/path ~user\"x\" /~ $: $")));
  EXPECT_EQ(t.kinds(Kind::TILDE), size_t(2));
  EXPECT_TRUE(t.text(0, ""));
  EXPECT_TRUE(t.text(2, "user"));
  EXPECT_EQ(t.kinds(Kind::PARAMETER), size_t(0));
  EXPECT_EQ(t.kinds(Kind::END_WORD), size_t(6));
}

TEST(LlvmLibcWordSyntaxTest, ContinuationsDoNotBreakExpansionTokens) {
  Tokens t;
  ASSERT_TRUE(bool(
      t.parse("$na\\\nme ~us\\\ner/ $\\\n{x\\\n:-word} $\\\n(\\\n(1)\\\n)")));
  EXPECT_EQ(t.kinds(Kind::PARAMETER), size_t(1));
  EXPECT_EQ(t.kinds(Kind::TILDE), size_t(1));
  EXPECT_EQ(t.kinds(Kind::BRACED_PARAMETER), size_t(1));
  EXPECT_EQ(t.kinds(Kind::ARITHMETIC), size_t(1));
  EXPECT_EQ(t.kinds(Kind::END_WORD), size_t(4));
  ASSERT_TRUE(bool(t.parse("'a\\\nb' $'c\\nd'")));
  EXPECT_TRUE(t.text(0, "a\\\nb"));
  EXPECT_TRUE(t.text(2, "c\\nd"));
}

TEST(LlvmLibcWordSyntaxTest, DollarQuoteErrorsPrecedeAnyEffects) {
  Tokens t;
  const char *bad[] = {"${x=changed} $'\\q'", "${x:-$'\\400'}",
                       "$'\\x123'", "$'\\0\\q'", "$'\\\n'"};
  for (auto input : bad) {
    EXPECT_TRUE(t.parse(input).error == Error::SYNTAX);
    EXPECT_EQ(t.count, size_t(0));
  }
}

TEST(LlvmLibcWordSyntaxTest, ParameterFragmentsKeepSpacesAndQuotes) {
  Tokens t;
  ASSERT_TRUE(bool(t.parse("\"${x:-\"$y\"}\"")));
  t.set("a b' c'\"$x\"");
  ASSERT_TRUE(bool(LIBC_NAMESPACE::internal::parse_word_fragment(
      {t.source, t.length}, false, false, true, {&t, Tokens::emit})));
  EXPECT_EQ(t.kinds(Kind::END_WORD), size_t(0));
  EXPECT_EQ(t.kinds(Kind::PARAMETER), size_t(1));
  EXPECT_TRUE(t.text(1, " "));
  t.set("(1 + ${x:-2}) * 3");
  ASSERT_TRUE(bool(LIBC_NAMESPACE::internal::parse_word_fragment(
      {t.source, t.length}, false, true, true, {&t, Tokens::emit})));
  EXPECT_EQ(t.kinds(Kind::BRACED_PARAMETER), size_t(1));
  const char *bad[] = {"(1+2", "1+2)"};
  for (auto input : bad) {
    t.set(input);
    EXPECT_FALSE(bool(LIBC_NAMESPACE::internal::parse_word_fragment(
        {t.source, t.length}, false, true, true, {&t, Tokens::emit})));
    EXPECT_EQ(t.count, size_t(0));
  }
}

TEST(LlvmLibcWordSyntaxTest, ScalarsAndNestingLimit) {
  Tokens t;
  uint32_t text[] = {0x4e00, '\\', '*', ' ', '"', 0x131, '"'};
  ASSERT_TRUE(bool(parse_word_syntax(text, true, {&t, Tokens::emit})));
  EXPECT_EQ(t.kinds(Kind::END_WORD), size_t(2));
  EXPECT_EQ(t.tokens[0].begin, size_t(0));
  EXPECT_EQ(t.tokens[0].end, size_t(1));
  t.length = t.count = 0;
  constexpr char opener[] = {'$', '{', 'x', '-'};
  for (unsigned i = 0; i != 130; ++i)
    for (char c : opener)
      t.source[t.length++] = c;
  t.source[t.length++] = 'a';
  for (unsigned i = 0; i != 130; ++i)
    t.source[t.length++] = '}';
  auto error =
      parse_word_syntax({t.source, t.length}, true, {&t, Tokens::emit});
  EXPECT_TRUE(error.error == Error::NESTING_LIMIT);
  EXPECT_EQ(t.count, size_t(0));
}
