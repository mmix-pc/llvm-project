//===-- POSIX formatted-output aliases and grouping
//------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/wint_t.h"
#include "src/stdio/snprintf.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

TEST(LlvmLibcPrintfExtensionsTest, WideAliases) {
  const bool modes[] = {false, true};
  for (bool utf8 : modes) {
    LocaleGuard locale(utf8);
    char output[32];
    EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%C%S",
                                       wint_t(L'A'), L"B"),
              2);
    EXPECT_STREQ(output, "AB");
#ifndef LIBC_COPT_PRINTF_DISABLE_INDEX_MODE
    // Looking ahead for the string pointer's type must understand S as well.
    EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%2$C%1$S", L"B",
                                       wint_t(L'A')),
              2);
    EXPECT_STREQ(output, "AB");
    EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%2$S%1$C",
                                       wint_t(L'A'), L"B"),
              2);
    EXPECT_STREQ(output, "BA");
#endif
  }
}

TEST(LlvmLibcPrintfExtensionsTest, EmptyGrouping) {
  const bool modes[] = {false, true};
  for (bool utf8 : modes) {
    LocaleGuard locale(utf8);
    char output[64];
    EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'d|%'08u",
                                       1234, 1234U),
              13);
    EXPECT_STREQ(output, "1234|00001234");
#ifndef LIBC_COPT_PRINTF_DISABLE_FLOAT
    EXPECT_EQ(
        LIBC_NAMESPACE::snprintf(output, sizeof(output), "%'0*.1f", 8, 1234.5),
        8);
    EXPECT_STREQ(output, "001234.5");
#endif
  }
}
