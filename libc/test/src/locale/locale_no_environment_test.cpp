//===-- Locale selection without a process environment --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale.h"
#include "src/locale/locale_storage.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;

TEST(LlvmLibcLocaleNoEnvironment, EmptyNameSelectsC) {
  auto locale = locale_from_mask(LOCALE_MASK);
  auto selected = resolve_locale(LOCALE_MASK, "", locale);
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(locale_mask(*selected), 0U);
  release_locale(*selected);
}

TEST(LlvmLibcLocaleNoEnvironment, SelectedCategoryOnly) {
  auto locale = locale_from_mask(LOCALE_MASK);
  auto selected = resolve_locale(LC_CTYPE_MASK, "", locale);
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(locale_mask(*selected), unsigned(LOCALE_MASK & ~LC_CTYPE_MASK));
  release_locale(*selected);
}

TEST(LlvmLibcLocaleNoEnvironment, ExplicitNames) {
  auto locale = locale_from_mask(0);
  auto selected = resolve_locale(LOCALE_MASK, "C.UTF-8", locale);
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(locale_mask(*selected), unsigned(LOCALE_MASK));
  EXPECT_FALSE(resolve_locale(LOCALE_MASK, "unknown", *selected).has_value());
  EXPECT_EQ(locale_mask(*selected), unsigned(LOCALE_MASK));
  auto c = resolve_locale(LOCALE_MASK, "POSIX", *selected);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(locale_mask(*c), 0U);
  release_locale(*selected);
  release_locale(*c);
}
