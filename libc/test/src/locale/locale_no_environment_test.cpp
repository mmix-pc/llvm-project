//===-- Locale selection without a process environment --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;

TEST(LlvmLibcLocaleNoEnvironment, EmptyNameSelectsC) {
  auto locale = locale_from_mask(LOCALE_MASK);
  ASSERT_TRUE(resolve_locale(LOCALE_MASK, "", locale));
  EXPECT_EQ(locale_mask(locale), 0U);
}

TEST(LlvmLibcLocaleNoEnvironment, SelectedCategoryOnly) {
  auto locale = locale_from_mask(LOCALE_MASK);
  ASSERT_TRUE(resolve_locale(LC_CTYPE_MASK, "", locale));
  EXPECT_EQ(locale_mask(locale), unsigned(LOCALE_MASK & ~LC_CTYPE_MASK));
}

TEST(LlvmLibcLocaleNoEnvironment, ExplicitNames) {
  auto locale = locale_from_mask(0);
  ASSERT_TRUE(resolve_locale(LOCALE_MASK, "C.UTF-8", locale));
  EXPECT_EQ(locale_mask(locale), unsigned(LOCALE_MASK));
  EXPECT_FALSE(resolve_locale(LOCALE_MASK, "unknown", locale));
  EXPECT_EQ(locale_mask(locale), unsigned(LOCALE_MASK));
  ASSERT_TRUE(resolve_locale(LOCALE_MASK, "POSIX", locale));
  EXPECT_EQ(locale_mask(locale), 0U);
}
