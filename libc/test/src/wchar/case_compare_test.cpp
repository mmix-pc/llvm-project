//===-- Wide case comparison tests ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcscasecmp.h"
#include "src/wchar/wcscasecmp_l.h"
#include "src/wchar/wcsncasecmp.h"
#include "src/wchar/wcsncasecmp_l.h"
#include "src/locale/locale.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
struct Selection {
  locale_t old = LIBC_NAMESPACE::internal::thread_locale;
  __locale_t c = LIBC_NAMESPACE::internal::locale_from_mask(0);
  __locale_t utf8 = LIBC_NAMESPACE::internal::locale_from_mask(1 << LC_CTYPE);
  Selection() { LIBC_NAMESPACE::internal::thread_locale = &c; }
  ~Selection() { LIBC_NAMESPACE::internal::thread_locale = old; }
};
} // namespace

TEST(LlvmLibcWideCaseCompareTest, BuiltinSignsAndBoundedStorage) {
  Selection selection;
  EXPECT_EQ(LIBC_NAMESPACE::wcscasecmp(L"", L""), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcscasecmp(L"aBc", L"AbC"), 0);
  EXPECT_LT(LIBC_NAMESPACE::wcscasecmp(L"A", L"ab"), 0);
  EXPECT_GT(LIBC_NAMESPACE::wcscasecmp(L"B", L"a"), 0);
  const wchar_t a[] = {L'A', L'B'}, b[] = {L'a', L'b'};
  EXPECT_EQ(LIBC_NAMESPACE::wcsncasecmp(a, b, 2), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcsncasecmp_l(a, b, 2, &selection.c), 0);
  // Both pointers are valid, but n=0 must not read even their first element.
  EXPECT_EQ(LIBC_NAMESPACE::wcsncasecmp(a + 2, b + 2, 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcsncasecmp_l(a + 2, b + 2, 0, &selection.c), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcsncasecmp(L"Aa", L"aB", 1), 0);
  EXPECT_LT(LIBC_NAMESPACE::wcsncasecmp(L"Aa", L"aB", 2), 0);
}

TEST(LlvmLibcWideCaseCompareTest, ExplicitAndCurrentSelection) {
  Selection selection;
  libc_errno = 17;
  EXPECT_NE(LIBC_NAMESPACE::wcscasecmp(L"\u00c4", L"\u00e4"), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcscasecmp_l(L"\u00c4", L"\u00e4", &selection.utf8), 0);
  LIBC_NAMESPACE::internal::thread_locale = &selection.utf8;
  EXPECT_EQ(LIBC_NAMESPACE::wcscasecmp(L"\u00c4", L"\u00e4"), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcsncasecmp(L"\u00c4", L"\u00e4", 1), 0);
  EXPECT_NE(LIBC_NAMESPACE::wcscasecmp_l(L"\u00c4", L"\u00e4", &selection.c), 0);
  EXPECT_EQ(int(libc_errno), 17);
}
