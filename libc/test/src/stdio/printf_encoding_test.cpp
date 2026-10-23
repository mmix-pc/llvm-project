//===-- Encoding failures and precision in formatted output ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/stdio/snprintf.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/src/locale/LocaleGuard.h"

using LlvmLibcPrintfEncodingTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcPrintfEncodingTest, InvalidWideString) {
  LocaleGuard locale;
  char output[32];
  const wchar_t invalid[] = {wchar_t(0xd800), L'\0'};
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%ls", invalid),
            -1);
  ASSERT_ERRNO_EQ(EILSEQ);
  const wchar_t prefixed[] = {L'A', wchar_t(0x110000), L'\0'};
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%ls", prefixed),
            -1);
  ASSERT_ERRNO_EQ(EILSEQ);
}

#ifdef LIBC_FULL_BUILD
TEST_F(LlvmLibcPrintfEncodingTest, UnrepresentableInCLocale) {
  LocaleGuard locale(false);
  char output[32];
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%ls", L"\u0100"),
            -1);
  ASSERT_ERRNO_EQ(EILSEQ);
}
#endif

TEST_F(LlvmLibcPrintfEncodingTest, PrecisionDoesNotSplitCharacters) {
  LocaleGuard locale;
  char output[32];
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.1ls", L"\u00a2"), 0);
  EXPECT_STREQ(output, "");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.2ls", L"\u00a2"), 2);
  EXPECT_STREQ(output, "\xc2\xa2");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%4.2ls", L"\u00a2"), 4);
  EXPECT_STREQ(output, "  \xc2\xa2");
  const wchar_t prefix_only[] = {L'A', wchar_t(0xd800)};
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.1ls", prefix_only),
      1);
  EXPECT_STREQ(output, "A");
  ASSERT_ERRNO_EQ(0);
}
