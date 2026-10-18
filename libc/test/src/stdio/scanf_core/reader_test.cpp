//===-- Unittests for the scanf String Reader -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/string_view.h"
#include "src/stdio/scanf_core/string_reader.h"

#include "test/UnitTest/Test.h"

TEST(LlvmLibcScanfStringReaderTest, Constructor) {
  char str[10];
  // buff_len justneeds to be a big number. The specific value isn't important
  // in the real world.
  LIBC_NAMESPACE::scanf_core::StringReader reader(const_cast<char *>(str),
                                                  1000000);
}

TEST(LlvmLibcScanfStringReaderTest, SimpleRead) {
  const char *str = "abc";
  LIBC_NAMESPACE::scanf_core::StringReader reader(const_cast<char *>(str),
                                                  1000000);

  for (size_t i = 0; i < sizeof("abc") - 1; ++i) {
    ASSERT_EQ(static_cast<int>(str[i]), reader.getc());
  }
  ASSERT_EQ(-1, reader.getc());
  ASSERT_EQ(-1, reader.getc());
}

TEST(LlvmLibcScanfStringReaderTest, ReadAndReverse) {
  const char *str = "abcDEF123";
  LIBC_NAMESPACE::scanf_core::StringReader reader(const_cast<char *>(str),
                                                  1000000);

  for (size_t i = 0; i < 5; ++i) {
    ASSERT_EQ(static_cast<int>(str[i]), reader.getc());
  }

  // Move back by 3, cursor should now be on 2
  reader.ungetc(str[4]);
  reader.ungetc(str[3]);
  reader.ungetc(str[2]);

  for (size_t i = 2; i < 7; ++i) {
    ASSERT_EQ(static_cast<int>(str[i]), reader.getc());
  }

  // Move back by 2, cursor should now be on 5
  reader.ungetc(str[6]);
  reader.ungetc(str[5]);

  for (size_t i = 5; i < 9; ++i) {
    ASSERT_EQ(static_cast<int>(str[i]), reader.getc());
  }
  ASSERT_EQ(-1, reader.getc());
  reader.ungetc(-1);

  // EOF does not advance the cursor. Move back over the nine input bytes.
  for (size_t i = 0; i < 9; ++i) {
    reader.ungetc(str[8 - i]);
  }

  // Check the whole string.
  for (size_t i = 0; i < sizeof("abcDEF123") - 1; ++i) {
    ASSERT_EQ(static_cast<int>(str[i]), reader.getc());
  }
  ASSERT_EQ(-1, reader.getc());
}

TEST(LlvmLibcScanfStringReaderTest, BoundedReadAndCount) {
  const char bytes[] = {'A', static_cast<char>(0xff)};
  LIBC_NAMESPACE::scanf_core::StringReader source(bytes, sizeof(bytes));
  LIBC_NAMESPACE::scanf_core::Reader<decltype(source)> &reader = source;
  EXPECT_EQ(reader.getc(), static_cast<int>('A'));
  EXPECT_EQ(reader.getc(), 255);
  EXPECT_EQ(reader.getc(), -1);
  reader.ungetc(-1);
  EXPECT_EQ(reader.chars_read(), size_t(2));
  reader.ungetc(255);
  EXPECT_EQ(reader.chars_read(), size_t(1));
  EXPECT_EQ(reader.getc(), 255);
}
