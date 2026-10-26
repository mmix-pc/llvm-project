//===-- Message plural expression tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/libintl/plural_forms.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::PluralForms;

TEST(LlvmLibcPluralFormsTest, CountsPrecedenceAndRightAssociativeConditional) {
  auto english = PluralForms::parse("nplurals=2; plural=(n != 1);");
  ASSERT_TRUE(english.has_value());
  EXPECT_EQ(english->size(), 2UL);
  EXPECT_EQ(english->select(0).value(), 1UL);
  EXPECT_EQ(english->select(1).value(), 0UL);
  EXPECT_EQ(english->select(2).value(), 1UL);
  auto other =
      PluralForms::parse(" plural = n==1 ? 0 : n==2 ? 1 : 2; nplurals=3 ");
  ASSERT_TRUE(other.has_value());
  EXPECT_EQ(other->select(1).value(), 0UL);
  EXPECT_EQ(other->select(2).value(), 1UL);
  EXPECT_EQ(other->select(3).value(), 2UL);
  auto arithmetic = PluralForms::parse("nplurals=100; plural=1+2*3+20/4-8%3;");
  ASSERT_TRUE(arithmetic.has_value());
  EXPECT_EQ(arithmetic->select(0).value(), 10UL);
  auto associativity = PluralForms::parse("nplurals=100;plural=20/2/2-1-1");
  ASSERT_TRUE(associativity.has_value());
  EXPECT_EQ(associativity->select(0).value(), 3UL);
}

TEST(LlvmLibcPluralFormsTest, SlavicIndependentBranchTable) {
  auto forms = PluralForms::parse(
      "nplurals=3; plural=n%10==1 && n%100!=11 ? 0 : "
      "n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2;");
  ASSERT_TRUE(forms.has_value());
  const unsigned long counts[] = {0,  1,  2,  4,  5,  11, 12,
                                  14, 20, 21, 22, 24, 25, 111};
  const unsigned long expected[] = {2, 0, 1, 1, 2, 2, 2, 2, 2, 0, 1, 1, 2, 2};
  for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
    EXPECT_EQ(forms->select(counts[i]).value(), expected[i]);
}

TEST(LlvmLibcPluralFormsTest, LazyEvaluationAndArithmeticErrors) {
  const char *valid[] = {
      "nplurals=2;plural=0 && (1/0)", "nplurals=2;plural=1 || (1%0)",
      "nplurals=2;plural=n ? 1 : 1/0", "nplurals=2;plural=!n || n > 0",
      "nplurals=2;plural=n<=1 && n>=1"};
  for (auto expression : valid) {
    auto forms = PluralForms::parse(expression);
    ASSERT_TRUE(forms.has_value());
    EXPECT_TRUE(forms->select(1).has_value());
  }
  auto divide = PluralForms::parse("nplurals=2;plural=n ? 1 : 1/0");
  ASSERT_TRUE(divide.has_value());
  EXPECT_EQ(divide->select(0).error(), EDOM);
  auto range = PluralForms::parse("nplurals=2;plural=n");
  ASSERT_TRUE(range.has_value());
  EXPECT_EQ(range->select(2).error(), EDOM);
  auto wrap = PluralForms::parse("nplurals=2;plural=(n+1)==0");
  ASSERT_TRUE(wrap.has_value());
  EXPECT_EQ(
      wrap->select(LIBC_NAMESPACE::cpp::numeric_limits<unsigned long>::max())
          .value(),
      1UL);
  auto subtract = PluralForms::parse("nplurals=2;plural=(0-n)>0");
  ASSERT_TRUE(subtract.has_value());
  EXPECT_EQ(subtract->select(1).value(), 1UL);
}

TEST(LlvmLibcPluralFormsTest, RejectMalformedAndOversizedMetadata) {
  const char *bad[] = {"",
                       "nplurals=0;plural=0",
                       "nplurals=2",
                       "plural=n!=1",
                       "nplurals=2;nplurals=2;plural=0",
                       "nplurals=2;plural=0;plural=1",
                       "nplurals=2;plural=1 garbage",
                       "nplurals=2;plural=n=1",
                       "nplurals=2;plural=n & 1",
                       "nplurals=2;plural=n | 1",
                       "nplurals=2;plural=-1",
                       "nplurals=2;plural=(n",
                       "nplurals=2;plural=n?1",
                       "nplurals=2;plural=n?1:",
                       "nplurals=2;plural=n?1:0 garbage",
                       "nplurals=2;plural=1 || (bad)",
                       "nplurals=2;plural=0?0:(1+)",
                       "nplurals=2;plural=0x1",
                       "nplurals=2;plural=nn",
                       "nplurals=2 plural=0"};
  for (auto text : bad)
    EXPECT_FALSE(PluralForms::parse(text).has_value());
  EXPECT_EQ(PluralForms::parse("nplurals=2;plural=9999999999999999999999999999")
                .error(),
            EOVERFLOW);
  char huge[PluralForms::MAX_LENGTH + 1]{};
  EXPECT_EQ(PluralForms::parse({huge, sizeof(huge)}).error(), EOVERFLOW);
  char nested[256];
  const char prefix[] = "nplurals=2;plural=";
  size_t at = 0;
  for (size_t i = 0; i != sizeof(prefix) - 1; ++i)
    nested[at++] = prefix[i];
  for (unsigned i = 0; i != PluralForms::MAX_DEPTH; ++i)
    nested[at++] = '(';
  nested[at++] = 'n';
  for (unsigned i = 0; i != PluralForms::MAX_DEPTH; ++i)
    nested[at++] = ')';
  EXPECT_EQ(PluralForms::parse({nested, at}).error(), EOVERFLOW);
  // A long flat expression also bounds evaluation depth, not just parser depth.
  at = sizeof(prefix) - 1;
  nested[at++] = 'n';
  for (unsigned i = 0; i != PluralForms::MAX_DEPTH; ++i) {
    nested[at++] = '+';
    nested[at++] = '1';
  }
  EXPECT_EQ(PluralForms::parse({nested, at}).error(), EOVERFLOW);
}

TEST(LlvmLibcPluralFormsTest, TreeDoesNotBorrowInput) {
  char input[] = "nplurals=2;plural=n!=1";
  auto forms = PluralForms::parse(input);
  ASSERT_TRUE(forms.has_value());
  for (auto &c : input)
    c = 'X';
  auto copied = *forms;
  EXPECT_EQ(copied.select(1).value(), 0UL);
  EXPECT_EQ(copied.select(42).value(), 1UL);
  EXPECT_FALSE(PluralForms().select(1).has_value());
}
