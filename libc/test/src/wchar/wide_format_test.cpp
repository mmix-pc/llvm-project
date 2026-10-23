//===-- Tests for wide formatted output -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fopen.h"
#include "src/stdio/fseek.h"
#include "src/wchar/fgetwc.h"
#include "src/wchar/fwide.h"
#include "src/wchar/fwprintf.h"
#include "src/wchar/vfwprintf.h"
#include "src/wchar/vswprintf.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/src/locale/LocaleGuard.h"
#include <stdarg.h>

static int format(wchar_t *buffer, size_t size, const wchar_t *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = LIBC_NAMESPACE::vswprintf(buffer, size, fmt, args);
  va_end(args);
  return result;
}

TEST(LlvmLibcWideFormatTest, BufferAndVaList) {
  wchar_t buffer[20];
  EXPECT_EQ(format(buffer, 20, L"%d %ls", 42, L"\u20ac"), 4);
  EXPECT_EQ(buffer[0], L'4');
  EXPECT_EQ(buffer[1], L'2');
  EXPECT_EQ(buffer[2], L' ');
  EXPECT_EQ(buffer[3], L'\u20ac');
  EXPECT_EQ(buffer[4], L'\0');
  EXPECT_EQ(format(buffer, 3, L"abcd"), -1);
  EXPECT_EQ(buffer[2], L'\0');
  EXPECT_EQ(format(nullptr, 0, L"abcd"), -1);
}

TEST(LlvmLibcWideFormatTest, VaListTruncationReportsOverflow) {
  wchar_t buffer[2] = {L'!', L'!'};
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(format(buffer, 2, L"ab"), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EOVERFLOW);
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(format(buffer, 0, L""), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EOVERFLOW);
}

TEST(LlvmLibcWideFormatTest, StreamOutput) {
  LocaleGuard locale;
  auto name = libc_make_test_file_path("wide-format.test");
  ::FILE *file = LIBC_NAMESPACE::fopen(name, "w+");
  ASSERT_NE(file, nullptr);
  int count = -1;
  EXPECT_EQ(LIBC_NAMESPACE::fwprintf(file, L"%d %ls%n", 42, L"\u20ac", &count),
            4);
  EXPECT_EQ(count, 4);
  EXPECT_EQ(LIBC_NAMESPACE::fseek(file, 0, SEEK_SET), 0);
  const wchar_t characters[] = {L'4', L'2', L' ', L'\u20ac'};
  for (wchar_t expected : characters)
    EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), static_cast<wint_t>(expected));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

using LlvmLibcWideFormatErrorTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcWideFormatErrorTest, ByteOrientationRejected) {
  auto name = libc_make_test_file_path("wide-format-byte.test");
  ::FILE *file = LIBC_NAMESPACE::fopen(name, "w+");
  ASSERT_NE(file, nullptr);
  EXPECT_LT(LIBC_NAMESPACE::fwide(file, -1), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fwprintf(file, L""), -1);
  ASSERT_ERRNO_EQ(EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}
