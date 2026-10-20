//===-- Stream extensions on memory streams -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/stdio_macros.h"
#include "hdr/func/free.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fflush.h"
#include "src/stdio/fmemopen.h"
#include "src/stdio/fread.h"
#include "src/stdio/fseek.h"
#include "src/stdio/ftell.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/open_memstream.h"
#include "src/stdio/setvbuf.h"
#include "src/stdio_ext/__fbufsize.h"
#include "src/stdio_ext/__fpending.h"
#include "src/stdio_ext/__fpurge.h"
#include "src/stdio_ext/__freadable.h"
#include "src/stdio_ext/__fwritable.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcStdioExtensionsMemoryTest, PurgeBeforePublication) {
  char *data = nullptr;
  size_t length = 0;
  FILE *file = LIBC_NAMESPACE::open_memstream(&data, &length);
  ASSERT_NE(file, static_cast<FILE *>(nullptr));
  char buffer[16];
  ASSERT_EQ(LIBC_NAMESPACE::setvbuf(file, buffer, _IOFBF, sizeof(buffer)), 0);
  EXPECT_EQ(LIBC_NAMESPACE::__fbufsize(file), sizeof(buffer));
  EXPECT_NE(LIBC_NAMESPACE::__fwritable(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fwrite("discard", 1, 7, file), size_t(7));
  EXPECT_EQ(LIBC_NAMESPACE::__fpending(file), size_t(7));
  size_t old_length = length;
  char *old_data = data;
  LIBC_NAMESPACE::__fpurge(file);
  EXPECT_EQ(length, old_length);
  EXPECT_EQ(data, old_data);
  EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 0L);
  EXPECT_EQ(LIBC_NAMESPACE::fwrite("keep", 1, 4, file), size_t(4));
  EXPECT_EQ(LIBC_NAMESPACE::fflush(file), 0);
  EXPECT_EQ(length, size_t(4));
  EXPECT_STREQ(data, "keep");
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
  ::free(data);
}

TEST(LlvmLibcStdioExtensionsMemoryTest, DiscardInputReadAhead) {
  char data[] = "abcdefgh";
  FILE *file = LIBC_NAMESPACE::fmemopen(data, 8, "r");
  ASSERT_NE(file, static_cast<FILE *>(nullptr));
  char buffer[4], byte = 0;
  ASSERT_EQ(LIBC_NAMESPACE::setvbuf(file, buffer, _IOFBF, sizeof(buffer)), 0);
  EXPECT_NE(LIBC_NAMESPACE::__freadable(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fread(&byte, 1, 1, file), size_t(1));
  EXPECT_EQ(byte, 'a');
  EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 1L);
  LIBC_NAMESPACE::__fpurge(file);
  EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 4L);
  EXPECT_EQ(LIBC_NAMESPACE::fread(&byte, 1, 1, file), size_t(1));
  EXPECT_EQ(byte, 'e');
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}
