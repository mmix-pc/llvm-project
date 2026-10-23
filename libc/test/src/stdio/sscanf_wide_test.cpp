//===-- String scanning allocation and multibyte tests --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/stdio_macros.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/sscanf.h"
#include "src/stdio/vsscanf.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"
#include <stdarg.h>

static int scan(const char *input, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = LIBC_NAMESPACE::vsscanf(input, format, args);
  va_end(args);
  return result;
}

TEST(LlvmLibcSScanfWideTest, CharacterWidthsAndCounts) {
  LocaleGuard locale;
  wchar_t out[4] = {L'!', L'!', L'!', L'!'};
  int count = -1;
  EXPECT_EQ(LIBC_NAMESPACE::sscanf("\xc2\xa2!", "%lc%n", out, &count), 1);
  EXPECT_EQ(out[0], wchar_t(0xa2));
  EXPECT_EQ(out[1], L'!');
  EXPECT_EQ(count, 2);
  EXPECT_EQ(scan("\xc2\xa2!", "%1ls%n", out, &count), 1);
  EXPECT_EQ(out[0], wchar_t(0xa2));
  EXPECT_EQ(out[1], L'\0');
  EXPECT_EQ(count, 2);
  EXPECT_EQ(scan("aaz", "%l[a]%n", out, &count), 1);
  EXPECT_EQ(out[0], L'a');
  EXPECT_EQ(out[1], L'a');
  EXPECT_EQ(out[2], L'\0');
  EXPECT_EQ(count, 2);
  EXPECT_EQ(scan("\xc2\xa2!", "%l[\xc2\xa2]%n", out, &count), 1);
  EXPECT_EQ(out[0], wchar_t(0xa2));
  EXPECT_EQ(out[1], L'\0');
  EXPECT_EQ(count, 2);
}

TEST(LlvmLibcSScanfWideTest, InvalidAndIncompleteSequences) {
  LocaleGuard locale;
  wchar_t out = L'!';
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(scan("\xff", "%lc", &out), EOF);
  EXPECT_EQ(out, L'!');
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(scan("\xc2", "%lc", &out), EOF);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  int first = 0;
  EXPECT_EQ(scan("7 \xff", "%d %lc", &first, &out), 1);
  EXPECT_EQ(first, 7);
  EXPECT_EQ(out, L'!');
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
}

TEST(LlvmLibcSScanfWideTest, AliasesAndAllocation) {
  LocaleGuard locale;
  wchar_t ch = 0;
  wchar_t str[3] = {};
  EXPECT_EQ(scan("\xc2\xa2 AB", "%C %S", &ch, str), 2);
  EXPECT_EQ(ch, wchar_t(0xa2));
  EXPECT_EQ(str[0], L'A');
  EXPECT_EQ(str[1], L'B');
  EXPECT_EQ(str[2], wchar_t(0));
  char *bytes = nullptr;
  wchar_t *wide = nullptr;
  EXPECT_EQ(scan("abc \xc2\xa2", "%ms %mls", &bytes, &wide), 2);
  ASSERT_NE(bytes, nullptr);
  ASSERT_NE(wide, nullptr);
  EXPECT_STREQ(bytes, "abc");
  EXPECT_EQ(wide[0], wchar_t(0xa2));
  EXPECT_EQ(wide[1], wchar_t(0));
  ::free(bytes);
  ::free(wide);
  EXPECT_EQ(scan("abz", "%m[ab]", &bytes), 1);
  EXPECT_STREQ(bytes, "ab");
  ::free(bytes);
  EXPECT_EQ(scan("ab", "%2mc", &bytes), 1);
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(bytes[0], 'a');
  EXPECT_EQ(bytes[1], 'b');
  ::free(bytes);
  EXPECT_EQ(scan("\xc2\xa2", "%mlc", &wide), 1);
  ASSERT_NE(wide, nullptr);
  EXPECT_EQ(wide[0], wchar_t(0xa2));
  ::free(wide);
}
