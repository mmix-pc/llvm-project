//===-- Locale name query lifetime tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "hdr/errno_macros.h"
#include "hdr/locale_macros.h"
#include "src/__support/CPP/string.h"
#include "src/__support/libc_errno.h"
#include "src/locale/duplocale.h"
#include "src/locale/freelocale.h"
#include "src/locale/getlocalename_l.h"
#include "src/locale/newlocale.h"
#include "src/locale/setlocale.h"
#include "src/locale/uselocale.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
AppProperties app;
}

TEST(LlvmLibcLocaleNameTest, ObjectQueriesRetainEarlierResults) {
  auto first = LIBC_NAMESPACE::newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
  auto second = LIBC_NAMESPACE::newlocale(LC_TIME_MASK, "C.UTF-8", nullptr);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  const char *all = LIBC_NAMESPACE::getlocalename_l(LC_ALL, first);
  const char *ctype = LIBC_NAMESPACE::getlocalename_l(LC_CTYPE, first);
  LIBC_NAMESPACE::cpp::string saved(all);
  EXPECT_STREQ(LIBC_NAMESPACE::getlocalename_l(LC_NUMERIC, first), "C");
  EXPECT_STREQ(LIBC_NAMESPACE::getlocalename_l(LC_TIME, second), "C.UTF-8");
  EXPECT_STRNE(LIBC_NAMESPACE::getlocalename_l(LC_ALL, second), saved.c_str());
  EXPECT_STREQ(all, saved.c_str());
  EXPECT_STREQ(ctype, "C.UTF-8");
  auto copy = LIBC_NAMESPACE::duplocale(first);
  ASSERT_NE(copy, nullptr);
  LIBC_NAMESPACE::freelocale(first);
  EXPECT_STREQ(LIBC_NAMESPACE::getlocalename_l(LC_ALL, copy), saved.c_str());
  LIBC_NAMESPACE::freelocale(second);
  EXPECT_STREQ(LIBC_NAMESPACE::getlocalename_l(LC_ALL, copy), saved.c_str());
  LIBC_NAMESPACE::freelocale(copy);
}

TEST(LlvmLibcLocaleNameTest, ReplacementAndFailure) {
  auto object = LIBC_NAMESPACE::newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
  ASSERT_NE(object, nullptr);
  const char *all = LIBC_NAMESPACE::getlocalename_l(LC_ALL, object);
  LIBC_NAMESPACE::cpp::string saved(all);
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_TIME_MASK, "missing", object),
            nullptr);
  EXPECT_STREQ(all, saved.c_str());
  object = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", object);
  ASSERT_NE(object, nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::getlocalename_l(LC_ALL, object), "C");
  LIBC_NAMESPACE::freelocale(object);
}

TEST(LlvmLibcLocaleNameTest, GlobalRoundTripAndSeparateReturnStorage) {
  LIBC_NAMESPACE::cpp::string original(
      LIBC_NAMESPACE::setlocale(LC_ALL, nullptr));
  ASSERT_NE(LIBC_NAMESPACE::setlocale(LC_ALL, "C"), nullptr);
  ASSERT_NE(LIBC_NAMESPACE::setlocale(LC_NUMERIC, "C.UTF-8"), nullptr);
  const char *name = LIBC_NAMESPACE::getlocalename_l(LC_ALL, LC_GLOBAL_LOCALE);
  LIBC_NAMESPACE::cpp::string saved(name);
  auto object = LIBC_NAMESPACE::newlocale(LC_TIME_MASK, "C.UTF-8", nullptr);
  ASSERT_NE(object, nullptr);
  auto old = LIBC_NAMESPACE::uselocale(object);
  EXPECT_STREQ(LIBC_NAMESPACE::getlocalename_l(LC_TIME, object), "C.UTF-8");
  EXPECT_STREQ(name, saved.c_str());
  EXPECT_NE(LIBC_NAMESPACE::setlocale(LC_ALL, "C"), nullptr);
  EXPECT_STREQ(name, saved.c_str());
  EXPECT_NE(LIBC_NAMESPACE::setlocale(LC_ALL, name), nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::getlocalename_l(LC_ALL, LC_GLOBAL_LOCALE),
               saved.c_str());
  LIBC_NAMESPACE::uselocale(old);
  LIBC_NAMESPACE::freelocale(object);
  LIBC_NAMESPACE::setlocale(LC_ALL, original.c_str());
}

TEST(LlvmLibcLocaleNameTest, UnsupportedCategoriesPreserveErrno) {
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::getlocalename_l(-1, LC_GLOBAL_LOCALE), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::getlocalename_l(LC_ALL + 1, LC_GLOBAL_LOCALE),
            nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}
