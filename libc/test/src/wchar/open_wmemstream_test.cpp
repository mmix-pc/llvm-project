//===-- Tests for wide memory output streams ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/stdio_macros.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fflush.h"
#include "src/stdio/fgetpos.h"
#include "src/stdio/fileno.h"
#include "src/stdio/fseek.h"
#include "src/stdio/fsetpos.h"
#include "src/stdio/ftell.h"
#include "src/wchar/fputwc.h"
#include "src/wchar/fputws.h"
#include "src/wchar/fwide.h"
#include "src/wchar/open_wmemstream.h"
#include "test/UnitTest/ErrnoCheckingTest.h"

using LlvmLibcWideMemstreamTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;
namespace LIBC = LIBC_NAMESPACE;

TEST_F(LlvmLibcWideMemstreamTest, WideUnitsAndPublishedLength) {
  wchar_t *memory;
  size_t length;
  FILE *f = LIBC::open_wmemstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  EXPECT_GT(LIBC::fwide(f, 0), 0);
  EXPECT_EQ(LIBC::ftell(f), 0L);
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(0));
  EXPECT_EQ(memory[0], L'\0');
  ASSERT_GE(LIBC::fputws(L"a\u20ac\U0001f642", f), 0);
  EXPECT_EQ(LIBC::ftell(f), 3L);
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(3));
  EXPECT_EQ(memory[0], L'a');
  EXPECT_EQ(memory[1], wchar_t(0x20ac));
  EXPECT_EQ(memory[2], wchar_t(0x1f642));
  EXPECT_EQ(memory[3], L'\0');
  ASSERT_EQ(LIBC::fseek(f, 1, SEEK_SET), 0);
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(1));
  ASSERT_EQ(LIBC::fseek(f, 0, SEEK_END), 0);
  EXPECT_EQ(LIBC::ftell(f), 3L);
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(length, size_t(3));
  ::free(memory);
}

TEST_F(LlvmLibcWideMemstreamTest, SeekDefersExtensionAndRestoresPositions) {
  wchar_t *memory;
  size_t length;
  FILE *f = LIBC::open_wmemstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fputwc(L'x', f), wint_t(L'x'));
  fpos_t saved;
  ASSERT_EQ(LIBC::fgetpos(f, &saved), 0);
  ASSERT_EQ(LIBC::fseek(f, 4, SEEK_SET), 0);
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(1));
  ASSERT_EQ(LIBC::fputwc(L'y', f), wint_t(L'y'));
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(5));
  for (unsigned i = 1; i < 4; ++i)
    EXPECT_EQ(memory[i], L'\0');
  EXPECT_EQ(memory[4], L'y');
  EXPECT_EQ(memory[5], L'\0');
  ASSERT_EQ(LIBC::fsetpos(f, &saved), 0);
  EXPECT_EQ(LIBC::ftell(f), 1L);
  ASSERT_EQ(LIBC::fputwc(L'z', f), wint_t(L'z'));
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(length, size_t(2));
  EXPECT_EQ(memory[1], L'z');
  EXPECT_EQ(memory[4], L'y');
  ::free(memory);
}

TEST_F(LlvmLibcWideMemstreamTest, InvalidSeekDoesNotChangePosition) {
  wchar_t *memory;
  size_t length;
  FILE *f = LIBC::open_wmemstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(LIBC::fseek(f, -1, SEEK_SET), -1);
  ASSERT_ERRNO_EQ(EINVAL);
  EXPECT_EQ(LIBC::ftell(f), 0L);
  EXPECT_EQ(LIBC::fileno(f), -1);
  ASSERT_ERRNO_EQ(EBADF);
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(length, size_t(0));
  EXPECT_EQ(memory[0], L'\0');
  ::free(memory);
}
