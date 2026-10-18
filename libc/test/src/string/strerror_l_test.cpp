//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/locale_macros.h"
#include "src/__support/StringUtil/platform_errors.h"
#include "src/locale/freelocale.h"
#include "src/locale/newlocale.h"
#include "src/string/strerror.h"
#include "src/string/strerror_l.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcStrerrorLTest, KnownErrors) {
  const char *names[] = {"C", ""};
  for (const char *name : names) {
    locale_t loc = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, name, nullptr);
    ASSERT_NE(loc, nullptr);
    for (auto [number, message] : LIBC_NAMESPACE::PLATFORM_ERRORS)
      EXPECT_STREQ(LIBC_NAMESPACE::strerror_l(static_cast<int>(number), loc),
                   message.begin());
    LIBC_NAMESPACE::freelocale(loc);
  }
}

TEST(LlvmLibcStrerrorLTest, UnknownErrorsAndLifetime) {
  locale_t loc = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", nullptr);
  ASSERT_NE(loc, nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::strerror_l(-1, loc), "Unknown error -1");
  EXPECT_STREQ(LIBC_NAMESPACE::strerror_l(2147483647, loc),
               "Unknown error 2147483647");
  EXPECT_STREQ(LIBC_NAMESPACE::strerror_l(-2147483647 - 1, loc),
               "Unknown error -2147483648");
  const char *known = LIBC_NAMESPACE::strerror_l(0, loc);
  const char *unknown = LIBC_NAMESPACE::strerror_l(-1234, loc);
  EXPECT_STREQ(known, "Success");
  EXPECT_STREQ(unknown, "Unknown error -1234");
  // Both APIs intentionally share the same per-thread scratch storage.
  EXPECT_STREQ(LIBC_NAMESPACE::strerror(-4321), "Unknown error -4321");
  EXPECT_STREQ(known, "Success");
  // Do not use unknown after the invalidating call above.
  LIBC_NAMESPACE::freelocale(loc);
}
