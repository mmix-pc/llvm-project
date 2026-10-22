//===-- Unittests for wcslcat ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/__support/macros/properties/os.h"
#include "src/wchar/wcslcat.h"
#include "test/UnitTest/Test.h"

#if !defined(LIBC_FULL_BUILD) && defined(LIBC_TARGET_OS_IS_LINUX)
#include "test/src/string/memory_utils/protected_pages.h"
#endif

TEST(LlvmLibcWCSLCatTest, TooBig) {
  const wchar_t *src = L"cd";
  wchar_t dst[4]{L"ab"};
  size_t res = LIBC_NAMESPACE::wcslcat(dst, src, 3);
  ASSERT_TRUE(dst[0] == L'a');
  ASSERT_TRUE(dst[1] == L'b');
  ASSERT_TRUE(dst[2] == L'\0');
  // Should still return src length + dst length
  ASSERT_EQ(res, size_t(4));
  // Not enough space to copy d
  res = LIBC_NAMESPACE::wcslcat(dst, src, 4);
  ASSERT_TRUE(dst[0] == L'a');
  ASSERT_TRUE(dst[1] == L'b');
  ASSERT_TRUE(dst[2] == L'c');
  ASSERT_TRUE(dst[3] == L'\0');
  ASSERT_EQ(res, size_t(4));
}

TEST(LlvmLibcWCSLCatTest, Smaller) {
  const wchar_t *src = L"cd";
  wchar_t dst[7]{L"ab"};
  size_t res = LIBC_NAMESPACE::wcslcat(dst, src, 7);
  ASSERT_TRUE(dst[0] == L'a');
  ASSERT_TRUE(dst[1] == L'b');
  ASSERT_TRUE(dst[2] == L'c');
  ASSERT_TRUE(dst[3] == L'd');
  ASSERT_TRUE(dst[4] == L'\0');
  ASSERT_EQ(res, size_t(4));
}

TEST(LlvmLibcWCSLCatTest, SmallerNoOverwriteAfter0) {
  const wchar_t *src = L"cd";
  wchar_t dst[8]{L"ab\0\0efg"};
  size_t res = LIBC_NAMESPACE::wcslcat(dst, src, 8);
  ASSERT_TRUE(dst[0] == L'a');
  ASSERT_TRUE(dst[1] == L'b');
  ASSERT_TRUE(dst[2] == L'c');
  ASSERT_TRUE(dst[3] == L'd');
  ASSERT_TRUE(dst[4] == L'\0');
  ASSERT_TRUE(dst[5] == L'f');
  ASSERT_TRUE(dst[6] == L'g');
  ASSERT_TRUE(dst[7] == L'\0');
  ASSERT_EQ(res, size_t(4));
}

TEST(LlvmLibcWCSLCatTest, DstsizeLessThanDstlen) {
  const wchar_t *src = L"d";
  wchar_t dst[4]{L"abc"};
  // Should return src length + dst size
  size_t res = LIBC_NAMESPACE::wcslcat(dst, src, 2);
  ASSERT_TRUE(dst[0] == L'a');
  ASSERT_TRUE(dst[1] == L'b');
  ASSERT_TRUE(dst[2] == L'c');
  ASSERT_TRUE(dst[3] == L'\0');
  ASSERT_EQ(res, size_t(3));
}

TEST(LlvmLibcWCSLCatTest, ZeroCapacity) {
  wchar_t dst = L'x';
  EXPECT_EQ(LIBC_NAMESPACE::wcslcat(&dst, L"abc", 0), size_t(3));
  EXPECT_EQ(dst, L'x');
}

TEST(LlvmLibcWCSLCatTest, UnterminatedDestination) {
  wchar_t dst[] = {L'a', L'b', L'c'};
  EXPECT_EQ(LIBC_NAMESPACE::wcslcat(dst, L"de", 3), size_t(5));
  EXPECT_EQ(dst[0], L'a');
  EXPECT_EQ(dst[1], L'b');
  EXPECT_EQ(dst[2], L'c');
}

TEST(LlvmLibcWCSLCatTest, OnlyTerminatorFits) {
  wchar_t dst[] = {L'a', L'\0', L'z'};
  EXPECT_EQ(LIBC_NAMESPACE::wcslcat(dst, L"bc", 2), size_t(3));
  EXPECT_EQ(dst[0], L'a');
  EXPECT_EQ(dst[1], L'\0');
  EXPECT_EQ(dst[2], L'z');
}

#if !defined(LIBC_FULL_BUILD) && defined(LIBC_TARGET_OS_IS_LINUX)
TEST(LlvmLibcWCSLCatTest, DoesNotReadPastCapacity) {
  ProtectedPages pages;
  auto page = pages.GetPageA().WithAccess(PROT_READ | PROT_WRITE);
  auto *dst = reinterpret_cast<wchar_t *>(page.top(3 * sizeof(wchar_t)));
  dst[0] = L'a';
  dst[1] = L'b';
  dst[2] = L'c';
  EXPECT_EQ(LIBC_NAMESPACE::wcslcat(dst, L"de", 3), size_t(5));
  EXPECT_EQ(dst[2], L'c');
  EXPECT_EQ(LIBC_NAMESPACE::wcslcat(dst + 3, L"de", 0), size_t(2));
}
#endif
