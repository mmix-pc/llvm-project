//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unittests for swprintf.
///
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "hdr/types/wint_t.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/libc_errno.h"
#include "src/wchar/swprintf.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/Test.h"

using LlvmLibcSwprintfTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

using LIBC_NAMESPACE::swprintf;
using LIBC_NAMESPACE::cpp::basic_string_view;

TEST_F(LlvmLibcSwprintfTest, EmptyAndZeroCapacity) {
  wchar_t buf[] = {L'!', L'!'};
  EXPECT_EQ(swprintf(buf, 0, L""), -1);
  EXPECT_EQ(buf[0], L'!');
  EXPECT_EQ(swprintf(buf, 1, L""), 0);
  EXPECT_EQ(buf[0], L'\0');
  EXPECT_EQ(buf[1], L'!');
  EXPECT_EQ(swprintf(buf, 1, L"x"), -1);
  EXPECT_EQ(buf[0], L'\0');
  ASSERT_ERRNO_EQ(0);
}

TEST_F(LlvmLibcSwprintfTest, ExactFitAndTruncation) {
  wchar_t buf[] = {L'!', L'!', L'!', L'!', L'!'};
  EXPECT_EQ(swprintf(buf + 1, 3, L"\u4e2dX"), 2);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf + 1) == L"\u4e2dX");
  EXPECT_EQ(swprintf(buf + 1, 3, L"\u4e2dXYZ"), -1);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf + 1) == L"\u4e2dX");
  EXPECT_EQ(buf[0], L'!');
  EXPECT_EQ(buf[4], L'!');
  ASSERT_ERRNO_EQ(0);
}

TEST_F(LlvmLibcSwprintfTest, PreservesErrno) {
  wchar_t buf[4];
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(swprintf(buf, 4, L"%d", 12), 2);
  EXPECT_EQ(swprintf(buf, 4, L"longer"), -1);
  EXPECT_EQ(swprintf(buf, 0, L""), -1);
  ASSERT_ERRNO_EQ(EDOM);
}

TEST_F(LlvmLibcSwprintfTest, MixedConversions) {
  wchar_t buf[128];
  EXPECT_EQ(swprintf(buf, 128, L"%+04d|%#x|%#o|%u|%%|%c|%lc|%.1s|%.1ls", -1,
                     31U, 8U, 42U, int('A'), wint_t(L'\u03bb'), "\xe4\xb8\xadX",
                     L"\u03bbX"),
            26);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf) ==
              L"-001|0x1f|010|42|%|A|\u03bb|\u4e2d|\u03bb");
}

TEST_F(LlvmLibcSwprintfTest, FieldsAndLengths) {
  wchar_t buf[128];
  EXPECT_EQ(swprintf(buf, 128, L"%*.*d|%hhd|%hd|%ld|%lld|%jd|%zu|%td", -5, 3, 1,
                     -2, -3, -4L, -5LL, intmax_t(-6), size_t(7), ptrdiff_t(-8)),
            25);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf) == L"001  |-2|-3|-4|-5|-6|7|-8");
  EXPECT_EQ(swprintf(buf, 128, L"%.0d|%#.0o", 0, 0U), 2);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf) == L"|0");
}

TEST_F(LlvmLibcSwprintfTest, FloatingAndPointer) {
  wchar_t buf[128];
#ifndef LIBC_COPT_PRINTF_DISABLE_FLOAT
  EXPECT_EQ(
      swprintf(buf, 128, L"%.1f|%.1e|%.2g|%a|%.1Lf", 1.5, 1.5, 1.5, 1.0, 1.5L),
      26);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf) == L"1.5|1.5e+00|1.5|0x1p+0|1.5");
#endif
  EXPECT_EQ(swprintf(buf, 128, L"%p", static_cast<void *>(nullptr)), 9);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf) == L"(nullptr)");
}

TEST_F(LlvmLibcSwprintfTest, CharactersStringsAndCounts) {
  wchar_t buf[32];
  EXPECT_EQ(swprintf(buf, 32, L"%c%lcX", 0, wint_t(0)), 3);
  EXPECT_EQ(buf[0], L'\0');
  EXPECT_EQ(buf[1], L'\0');
  EXPECT_EQ(buf[2], L'X');
  EXPECT_EQ(buf[3], L'\0');
#ifndef LIBC_COPT_PRINTF_DISABLE_WRITE_INT
  int n = -1;
  EXPECT_EQ(swprintf(buf, 32, L"%s%ls%n", "\xe4\xb8\xad", L"\u03bb", &n), 2);
  EXPECT_EQ(n, 2);
  n = -1;
  EXPECT_EQ(swprintf(buf, 2, L"%ls%n", L"AB", &n), -1);
  EXPECT_EQ(n, -1);
#endif
}

TEST_F(LlvmLibcSwprintfTest, EncodingError) {
  wchar_t buf[32];
  EXPECT_EQ(swprintf(buf, 32, L"A%s", "\xc0\xaf"), -1);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf) == L"A");
  ASSERT_ERRNO_EQ(EILSEQ);
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(swprintf(buf, 32, L"%c", 0x80), -1);
  ASSERT_ERRNO_EQ(EILSEQ);
}

TEST_F(LlvmLibcSwprintfTest, CountOverflow) {
  wchar_t buf[4];
  // The leading character leaves fewer than INT_MAX representable elements.
  EXPECT_EQ(swprintf(buf, 4, L"x%*ls", INT_MAX, L""), -1);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf) == L"x");
  ASSERT_ERRNO_EQ(EOVERFLOW);
}

#ifndef LIBC_COPT_PRINTF_DISABLE_INDEX_MODE
TEST_F(LlvmLibcSwprintfTest, IndexedArguments) {
  wchar_t buf[32];
  EXPECT_EQ(swprintf(buf, 32, L"%2$ls %1$d", 42, L"\u4e2d"), 4);
  EXPECT_TRUE(basic_string_view<wchar_t>(buf) == L"\u4e2d 42");
}
#endif
