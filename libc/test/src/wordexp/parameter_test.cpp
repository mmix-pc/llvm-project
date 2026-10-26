//===-- Parameter expansion operator tests --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/wordexp/parameter.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::cpp::string_view;
using LIBC_NAMESPACE::internal::ParameterExpansion;
using LIBC_NAMESPACE::internal::parse_word_parameter;
using Action = ParameterExpansion::Action;
using Op = ParameterExpansion::Operation;

TEST(LlvmLibcWordParameterTest, ValueLengthAndLiteralWordSlices) {
  auto value = parse_word_parameter("_value123");
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->operation == Op::VALUE);
  EXPECT_TRUE(value->name == "_value123");
  EXPECT_TRUE(value->word.empty());
  EXPECT_TRUE(value->select(true, false) == Action::VALUE);
  auto length = parse_word_parameter("#value");
  ASSERT_TRUE(length.has_value());
  EXPECT_TRUE(length->name == "value");
  EXPECT_TRUE(length->select(true, false) == Action::LENGTH);
  const char input[] = "value:-\"${other:-$(command)}\"";
  auto nested = parse_word_parameter(input);
  ASSERT_TRUE(nested.has_value());
  EXPECT_TRUE(nested->word == "\"${other:-$(command)}\"");
  EXPECT_EQ(nested->word.data(), input + 7);
  // Branch selection neither reads variables nor interprets command text.
  EXPECT_TRUE(nested->select(true, false) == Action::VALUE);
  EXPECT_TRUE(nested->select(false, false) == Action::WORD);
}

TEST(LlvmLibcWordParameterTest, CompleteColonDecisionTable) {
  struct Case {
    const char *text;
    Action nonempty, empty, unset;
  };
  Case cases[] = {
      {"x-word", Action::VALUE, Action::VALUE, Action::WORD},
      {"x:-word", Action::VALUE, Action::WORD, Action::WORD},
      {"x=word", Action::VALUE, Action::VALUE, Action::ASSIGN_WORD},
      {"x:=word", Action::VALUE, Action::ASSIGN_WORD, Action::ASSIGN_WORD},
      {"x?word", Action::VALUE, Action::VALUE, Action::ERROR_WORD},
      {"x:?word", Action::VALUE, Action::ERROR_WORD, Action::ERROR_WORD},
      {"x+word", Action::WORD, Action::WORD, Action::EMPTY},
      {"x:+word", Action::WORD, Action::EMPTY, Action::EMPTY},
  };
  for (auto item : cases) {
    auto parameter = parse_word_parameter(item.text);
    ASSERT_TRUE(parameter.has_value());
    EXPECT_TRUE(parameter->word == "word");
    EXPECT_TRUE(parameter->select(true, false) == item.nonempty);
    EXPECT_TRUE(parameter->select(true, true) == item.empty);
    EXPECT_TRUE(parameter->select(false, true) == item.unset);
  }
  const char *omitted[] = {"x-", "x:-", "x=", "x:=", "x?", "x:?", "x+", "x:+"};
  for (auto text : omitted) {
    auto parameter = parse_word_parameter(text);
    ASSERT_TRUE(parameter.has_value());
    EXPECT_TRUE(parameter->word.empty());
  }
}

TEST(LlvmLibcWordParameterTest, PatternRemovalAndMalformedOperators) {
  struct Case {
    const char *text;
    Op operation;
  };
  Case cases[] = {{"x#*a", Op::PREFIX},
                  {"x##*a", Op::LONGEST_PREFIX},
                  {"x%*a", Op::SUFFIX},
                  {"x%%*a", Op::LONGEST_SUFFIX}};
  for (auto item : cases) {
    auto parameter = parse_word_parameter(item.text);
    ASSERT_TRUE(parameter.has_value());
    EXPECT_TRUE(parameter->operation == item.operation);
    EXPECT_TRUE(parameter->word == "*a");
    EXPECT_TRUE(parameter->select(true, false) == Action::PATTERN);
  }
  const char *invalid[] = {"",     "#",    "9name",   " x",     "x:",    "x:/a",
                           "x:#a", "x:%a", "#x-word", "x/word", "x word"};
  for (auto text : invalid)
    EXPECT_EQ(parse_word_parameter(text).error(), EINVAL);
}
