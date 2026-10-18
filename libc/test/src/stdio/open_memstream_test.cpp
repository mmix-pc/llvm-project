//===-- Tests for dynamically growing memory streams ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/stdio_macros.h"
#include "src/__support/File/memory_file.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fflush.h"
#include "src/stdio/fileno.h"
#include "src/stdio/fputc.h"
#include "src/stdio/fread.h"
#include "src/stdio/fseek.h"
#include "src/stdio/fseeko.h"
#include "src/stdio/ftell.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/open_memstream.h"
#include "src/stdio/setvbuf.h"
#include "test/UnitTest/ErrnoCheckingTest.h"

using LlvmLibcOpenMemstreamTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;
namespace LIBC = LIBC_NAMESPACE;

TEST_F(LlvmLibcOpenMemstreamTest, EmptyFlushAndClose) {
  char *memory = nullptr;
  size_t length = 99;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fflush(f), 0);
  ASSERT_NE(memory, nullptr);
  EXPECT_EQ(length, size_t(0));
  EXPECT_EQ(memory[0], '\0');
  EXPECT_EQ(LIBC::fileno(f), -1);
  ASSERT_ERRNO_EQ(EBADF);
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(memory[0], '\0');
  ::free(memory);

  memory = nullptr;
  f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(length, size_t(0));
  ASSERT_NE(memory, nullptr);
  EXPECT_STREQ(memory, "");
  ::free(memory);
}

TEST_F(LlvmLibcOpenMemstreamTest, GapOverwriteAndInvalidSeek) {
  char *memory = nullptr;
  size_t length = 0;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fseek(f, 1, SEEK_CUR), 0);
  ASSERT_EQ(LIBC::fputc('q', f), int('q'));
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(2));
  EXPECT_EQ(memory[0], '\0');
  EXPECT_EQ(memory[1], 'q');
  EXPECT_EQ(memory[2], '\0');
  EXPECT_EQ(LIBC::fseek(f, -3, SEEK_CUR), -1);
  ASSERT_ERRNO_EQ(EINVAL);
  EXPECT_EQ(LIBC::ftell(f), 2L);
  ASSERT_EQ(LIBC::fseek(f, -2, SEEK_CUR), 0);
  ASSERT_EQ(LIBC::fputc('e', f), int('e'));
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_STREQ(memory, "eq");
  EXPECT_EQ(length, size_t(1));
  ASSERT_EQ(LIBC::fseek(f, 0, SEEK_END), 0);
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(2));
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_STREQ(memory, "eq");
  ::free(memory);
}

TEST_F(LlvmLibcOpenMemstreamTest, GrowthAndGlobalFlush) {
  char *memory = nullptr;
  size_t length = 0;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  for (unsigned i = 0; i != 4097; ++i)
    ASSERT_EQ(LIBC::fputc('a' + i % 26, f), int('a' + i % 26));
  ASSERT_EQ(LIBC::fflush(nullptr), 0);
  EXPECT_EQ(length, size_t(4097));
  for (unsigned i = 0; i != length; ++i)
    EXPECT_EQ(memory[i], char('a' + i % 26));
  EXPECT_EQ(memory[length], '\0');
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(memory[length], '\0');
  ::free(memory);
}

TEST_F(LlvmLibcOpenMemstreamTest, CustomBufferAndWriteOnly) {
  char *memory = nullptr;
  size_t length;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  char buffer[8];
  ASSERT_EQ(LIBC::setvbuf(f, buffer, _IOFBF, sizeof(buffer)), 0);
  ASSERT_EQ(LIBC::fwrite("abc", 1, 3, f), size_t(3));
  ASSERT_EQ(LIBC::fflush(f), 0);
  char c;
  EXPECT_EQ(LIBC::fread(&c, 1, 1, f), size_t(0));
  ASSERT_ERRNO_EQ(EBADF);
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_STREQ(memory, "abc");
  ::free(memory);
}

TEST_F(LlvmLibcOpenMemstreamTest, OverflowLeavesContentsAndPosition) {
  char *memory = nullptr;
  size_t length;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::setvbuf(f, nullptr, _IONBF, 0), 0);
  ASSERT_EQ(LIBC::fwrite("abc", 1, 3, f), size_t(3));
  ASSERT_EQ(LIBC::fseeko(f, LIBC::MEMORY_FILE_MAX, SEEK_SET), 0);
  EXPECT_EQ(LIBC::fputc('x', f), EOF);
  ASSERT_ERRNO_EQ(EFBIG);
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(3));
  EXPECT_STREQ(memory, "abc");
  EXPECT_EQ(LIBC::fseeko(f, 1, SEEK_CUR), -1);
  ASSERT_ERRNO_EQ(EINVAL);
  ASSERT_EQ(LIBC::fclose(f), 0);
  ::free(memory);
}
