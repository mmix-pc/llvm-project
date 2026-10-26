//===-- Word expansion arithmetic tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/wordexp/arithmetic.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::cpp::string_view;
using LIBC_NAMESPACE::internal::ArithmeticVariables;
using LIBC_NAMESPACE::internal::evaluate_wordexp_arithmetic;

namespace {
struct Variables {
  long x = 7, y = 3;
  unsigned reads = 0, writes = 0;
  int write_error = 0;
  ArithmeticVariables callbacks() { return {this, read, write}; }
  static ErrorOr<long> read(void *context, string_view name) {
    auto &self = *static_cast<Variables *>(context);
    ++self.reads;
    if (name == "x")
      return self.x;
    if (name == "y")
      return self.y;
    return Error(ENOENT);
  }
  static int write(void *context, string_view name, long value) {
    auto &self = *static_cast<Variables *>(context);
    ++self.writes;
    if (self.write_error)
      return self.write_error;
    if (name == "x")
      self.x = value;
    else if (name == "y")
      self.y = value;
    else
      return EACCES;
    return 0;
  }
};

void expect_value(string_view text, long expected) {
  auto result = evaluate_wordexp_arithmetic(text);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);
}
} // namespace

TEST(LlvmLibcWordexpArithmeticTest, ConstantsAndOperatorPrecedence) {
  expect_value("", 0);
  expect_value(" \t\n\r", 0);
  expect_value("0x20 + 020 + 10", 58);
  expect_value("1 + 2 * 3 - 20 / 4 + 8 % 3", 4);
  expect_value("20 / 2 / 2 - 1 - 1", 3);
  expect_value("1 << 2 + 1", 8);
  expect_value("32 >> 1 + 2", 4);
  expect_value("1 < 2 == 3 > 2", 1);
  expect_value("2 <= 2 && 3 >= 4 || 5 != 0", 1);
  expect_value("1 | 2 ^ 3 & 4", 3);
  expect_value("~0 & 15", 15);
  expect_value("!0 + !1", 1);
  expect_value("-7 / 3", -2);
  expect_value("-7 % 3", -1);
  expect_value("1 - -2", 3);
  expect_value("-8 >> 2", -2);
  expect_value("0 ? 1 : 0 ? 2 : 3", 3);
  expect_value("1 ? 0 ? 2 : 3 : 4", 3);
}

TEST(LlvmLibcWordexpArithmeticTest, LazyEvaluationAndSyntaxPreflight) {
  Variables variables;
  const char *lazy[] = {"0 && missing", "1 || missing", "1 ? 42 : (missing=3)",
                        "0 ? 1/0 : 42", "1 || (x=2)",   "0 && (x/=0)"};
  for (auto text : lazy) {
    auto result = evaluate_wordexp_arithmetic(text, variables.callbacks());
    EXPECT_TRUE(result.has_value());
  }
  EXPECT_EQ(variables.reads, 0U);
  EXPECT_EQ(variables.writes, 0U);
  EXPECT_EQ(variables.x, 7L);

  const char *invalid[] = {"(x=1) +",   "(x=1) + 08",    "x=1 garbage",
                           "1 || (x=)", "0 ? (1=2) : 3", "1 ? 2 : ("};
  for (auto text : invalid) {
    EXPECT_EQ(evaluate_wordexp_arithmetic(text, variables.callbacks()).error(),
              EINVAL);
  }
  EXPECT_EQ(variables.reads, 0U);
  EXPECT_EQ(variables.writes, 0U);
}

TEST(LlvmLibcWordexpArithmeticTest, AssignmentAndLvalues) {
  Variables variables;
  auto run = [&](const char *text, long expected) {
    auto result = evaluate_wordexp_arithmetic(text, variables.callbacks());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expected);
  };
  run("x=y=12", 12);
  EXPECT_EQ(variables.reads, 0U);
  EXPECT_EQ(variables.writes, 2U);
  EXPECT_EQ(variables.x, 12L);
  EXPECT_EQ(variables.y, 12L);
  run("(x)+=3", 15);
  run("x-=2", 13);
  run("x*=3", 39);
  run("x/=3", 13);
  run("x%=5", 3);
  run("x<<=2", 12);
  run("x>>=1", 6);
  run("x&=3", 2);
  run("x^=7", 5);
  run("x|=2", 7);
  run("x?y=4:5", 4);
  EXPECT_EQ(variables.y, 4L);
  const char *bad[] = {"1=2", "(x+1)=2", "(x=1)=2", "(x?y:x)=1", "++x",
                       "x++", "x--",     "--x",     "1++2"};
  unsigned writes = variables.writes;
  for (auto text : bad)
    EXPECT_EQ(evaluate_wordexp_arithmetic(text, variables.callbacks()).error(),
              EINVAL);
  EXPECT_EQ(variables.writes, writes);
}

TEST(LlvmLibcWordexpArithmeticTest, CallbackFailuresAndNoShellEvaluation) {
  Variables variables;
  EXPECT_EQ(
      evaluate_wordexp_arithmetic("missing", variables.callbacks()).error(),
      ENOENT);
  EXPECT_EQ(
      evaluate_wordexp_arithmetic("missing=1", variables.callbacks()).error(),
      EACCES);
  variables.write_error = ENOMEM;
  EXPECT_EQ(evaluate_wordexp_arithmetic("x=1", variables.callbacks()).error(),
            ENOMEM);
  EXPECT_EQ(variables.x, 7L);
  EXPECT_EQ(evaluate_wordexp_arithmetic("x").error(), EINVAL);
  const char *bad[] = {"$(command)", "`command`", "${x}",      "1;command",
                       "'1'",        "1,2",       "sizeof(x)", "0b10",
                       "0x",         "09",        "1 2"};
  unsigned reads = variables.reads, writes = variables.writes;
  for (auto text : bad)
    EXPECT_EQ(evaluate_wordexp_arithmetic(text, variables.callbacks()).error(),
              EINVAL);
  EXPECT_EQ(variables.reads, reads);
  EXPECT_EQ(variables.writes, writes);
}

TEST(LlvmLibcWordexpArithmeticTest, CheckedArithmeticBoundaries) {
  Variables variables;
  variables.x = LIBC_NAMESPACE::cpp::numeric_limits<long>::max();
  variables.y = LIBC_NAMESPACE::cpp::numeric_limits<long>::min();
  const char *overflow[] = {"x+1",  "y-1",  "x*2",   "-y",  "y/-1",
                            "y%-1", "x<<1", "-1<<1", "x+=1"};
  for (auto text : overflow)
    EXPECT_EQ(evaluate_wordexp_arithmetic(text, variables.callbacks()).error(),
              EOVERFLOW);
  EXPECT_EQ(variables.writes, 0U);
  const char *domain[] = {"1/0", "1%0", "1<<-1", "1>>-1", "1<<128", "1>>128"};
  for (auto text : domain)
    EXPECT_EQ(evaluate_wordexp_arithmetic(text).error(), EDOM);
  EXPECT_EQ(
      evaluate_wordexp_arithmetic("99999999999999999999999999999").error(),
      EOVERFLOW);
  expect_value("0 && (99999999999999999999999999999 / 0)", 0);
  if constexpr (sizeof(long) == 8) {
    expect_value("-9223372036854775808", variables.y);
    expect_value("-0x8000000000000000", variables.y);
    EXPECT_EQ(evaluate_wordexp_arithmetic("9223372036854775808").error(),
              EOVERFLOW);
    EXPECT_EQ(evaluate_wordexp_arithmetic("--9223372036854775808").error(),
              EINVAL);
  }
}

TEST(LlvmLibcWordexpArithmeticTest, BoundedRecursionAndFlatExpressions) {
  char nested[601];
  for (unsigned i = 0; i != 300; ++i)
    nested[i] = '(';
  nested[300] = '1';
  for (unsigned i = 301; i != sizeof(nested); ++i)
    nested[i] = ')';
  EXPECT_EQ(evaluate_wordexp_arithmetic({nested, sizeof(nested)}).error(),
            EOVERFLOW);
  char flat[601];
  flat[0] = '1';
  for (unsigned i = 1; i < sizeof(flat); i += 2) {
    flat[i] = '+';
    flat[i + 1] = '1';
  }
  expect_value({flat, sizeof(flat)}, 301);
}
