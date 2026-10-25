//===-- Display width tests -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcwidth.h"
#include "src/wchar/wcswidth.h"
#include "src/locale/locale.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
struct Selection {
  locale_t previous = LIBC_NAMESPACE::internal::thread_locale;
  __locale_t locale;
  explicit Selection(unsigned mask)
      : locale(LIBC_NAMESPACE::internal::locale_from_mask(mask)) {
    LIBC_NAMESPACE::internal::thread_locale = &locale;
  }
  ~Selection() { LIBC_NAMESPACE::internal::thread_locale = previous; }
};
} // namespace

TEST(LlvmLibcDisplayWidthTest, CLocaleAndBounds) {
  Selection selected(0);
  libc_errno = 17;
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(L'A'), 1);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(L' '), 1);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(L'\n'), -1);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(L'\u4e2d'), -1);
  EXPECT_EQ(LIBC_NAMESPACE::wcswidth(L"A B", 3), 3);
  EXPECT_EQ(LIBC_NAMESPACE::wcswidth(L"A\n", 2), -1);
  const wchar_t exact[] = {L'A', L'B'};
  EXPECT_EQ(LIBC_NAMESPACE::wcswidth(exact, 2), 2);
  EXPECT_EQ(LIBC_NAMESPACE::wcswidth(exact + 2, 0), 0);
  EXPECT_EQ(int(libc_errno), 17);
}

TEST(LlvmLibcDisplayWidthTest, UnicodeScalarWidths) {
  Selection selected(1 << LC_CTYPE);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(L'\u0301'), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(L'\u4e2d'), 2);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(L'\u00e9'), 1);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(L'\U0001f600'), 2);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(static_cast<wchar_t>(0xd800)), -1);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(static_cast<wchar_t>(0x110000)), -1);
  EXPECT_EQ(LIBC_NAMESPACE::wcwidth(static_cast<wchar_t>(-1)), -1);
  EXPECT_EQ(LIBC_NAMESPACE::wcswidth(L"A\u0301\u4e2d\U0001f600", 4), 5);
  EXPECT_EQ(LIBC_NAMESPACE::wcswidth(L"A\u0301\u4e2d\U0001f600", 3), 3);
  EXPECT_EQ(LIBC_NAMESPACE::wcswidth(L"A\0\u4e2d", 3), 1);
}
