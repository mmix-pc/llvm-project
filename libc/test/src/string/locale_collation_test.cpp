//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/locale_macros.h"
#include "src/locale/freelocale.h"
#include "src/locale/newlocale.h"
#include "src/string/strcmp.h"
#include "src/string/strcoll_l.h"
#include "src/string/strxfrm_l.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcLocaleCollationTest, SupportedLocales) {
  const char *names[] = {"C", ""};
  for (const char *name : names) {
    locale_t loc = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, name, nullptr);
    ASSERT_NE(loc, nullptr);
    EXPECT_EQ(LIBC_NAMESPACE::strcoll_l("", "", loc), 0);
    EXPECT_LT(LIBC_NAMESPACE::strcoll_l("", "a", loc), 0);
    EXPECT_GT(LIBC_NAMESPACE::strcoll_l("a", "", loc), 0);
    EXPECT_EQ(LIBC_NAMESPACE::strcoll_l("abc", "abc", loc), 0);
    EXPECT_LT(LIBC_NAMESPACE::strcoll_l("abc", "abcd", loc), 0);
    LIBC_NAMESPACE::freelocale(loc);
  }
  // Unsupported named locales must not silently become the C locale.
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "not-a-locale", nullptr),
            nullptr);
}

TEST(LlvmLibcLocaleCollationTest, UnsignedByteOrdering) {
  locale_t loc = LIBC_NAMESPACE::newlocale(LC_COLLATE_MASK, "C", nullptr);
  ASSERT_NE(loc, nullptr);
  for (int value = 1; value < 256; ++value) {
    const char higher[] = {'a', static_cast<char>(value), '\0'};
    const char lower[] = {'a', static_cast<char>(value - 1), '\0'};
    EXPECT_GT(LIBC_NAMESPACE::strcoll_l(higher, lower, loc), 0);
    EXPECT_LT(LIBC_NAMESPACE::strcoll_l(lower, higher, loc), 0);
    EXPECT_EQ(LIBC_NAMESPACE::strcoll_l(higher, higher, loc), 0);
  }
  LIBC_NAMESPACE::freelocale(loc);
}

TEST(LlvmLibcLocaleCollationTest, TransformationBounds) {
  locale_t loc = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", nullptr);
  ASSERT_NE(loc, nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::strxfrm_l(nullptr, "abc", 0, loc), size_t(3));
  EXPECT_EQ(LIBC_NAMESPACE::strxfrm_l(nullptr, "", 0, loc), size_t(0));
  char dest[] = {'x', 'x', 'x', 'x', 'x'};
  EXPECT_EQ(LIBC_NAMESPACE::strxfrm_l(dest, "abc", 0, loc), size_t(3));
  EXPECT_EQ(dest[0], 'x');
  for (size_t n = 1; n <= 3; ++n) {
    EXPECT_EQ(LIBC_NAMESPACE::strxfrm_l(dest, "abc", n, loc), size_t(3));
    // Insufficient-space contents are unspecified; only check the boundary.
    EXPECT_EQ(dest[n], 'x');
  }
  EXPECT_EQ(LIBC_NAMESPACE::strxfrm_l(dest, "abc", 4, loc), size_t(3));
  EXPECT_STREQ(dest, "abc");
  EXPECT_EQ(dest[4], 'x');
  dest[1] = 'x';
  EXPECT_EQ(LIBC_NAMESPACE::strxfrm_l(dest, "", 1, loc), size_t(0));
  EXPECT_EQ(dest[0], '\0');
  EXPECT_EQ(dest[1], 'x');
  LIBC_NAMESPACE::freelocale(loc);
}

TEST(LlvmLibcLocaleCollationTest, TransformedOrdering) {
  locale_t loc = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", nullptr);
  ASSERT_NE(loc, nullptr);
  const char *strings[] = {"", "a", "aa", "b", "\x7f", "\x80", "\xff"};
  for (const char *left : strings) {
    for (const char *right : strings) {
      char transformed_left[4], transformed_right[4];
      ASSERT_LT(LIBC_NAMESPACE::strxfrm_l(transformed_left, left, 4, loc),
                size_t(4));
      ASSERT_LT(LIBC_NAMESPACE::strxfrm_l(transformed_right, right, 4, loc),
                size_t(4));
      int collation = LIBC_NAMESPACE::strcoll_l(left, right, loc);
      int transformed =
          LIBC_NAMESPACE::strcmp(transformed_left, transformed_right);
      EXPECT_EQ(collation < 0, transformed < 0);
      EXPECT_EQ(collation == 0, transformed == 0);
      EXPECT_EQ(collation > 0, transformed > 0);
    }
  }
  LIBC_NAMESPACE::freelocale(loc);
}
