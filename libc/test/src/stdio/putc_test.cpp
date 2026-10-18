//===-- Unittests for putc ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/stdio_macros.h"
#include "src/stdio/fclose.h"
#include "src/stdio/ferror.h"
#include "src/stdio/fopen.h"
#include "src/stdio/fread.h"

#include "src/stdio/putc.h"

#include "test/UnitTest/Test.h"

TEST(LlvmLibcPutcTest, WriteToFile) {
  constexpr char FILENAME[] = "testdata/putc_output.test";
  ::FILE *file = LIBC_NAMESPACE::fopen(FILENAME, "w");
  ASSERT_FALSE(file == nullptr);

  constexpr char simple[] = "simple letters";
  for (size_t i = 0; i < sizeof(simple); ++i) {
    ASSERT_EQ(LIBC_NAMESPACE::putc(simple[i], file),
              static_cast<int>(static_cast<unsigned char>(simple[i])));
  }

  ASSERT_EQ(0, LIBC_NAMESPACE::fclose(file));

  file = LIBC_NAMESPACE::fopen(FILENAME, "r");
  ASSERT_FALSE(file == nullptr);
  char data[50];

  ASSERT_EQ(LIBC_NAMESPACE::fread(data, 1, sizeof(simple) - 1, file),
            sizeof(simple) - 1);
  data[sizeof(simple) - 1] = '\0';

  ASSERT_STREQ(data, simple);

  ASSERT_EQ(LIBC_NAMESPACE::ferror(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::putc('L', file), EOF);
  EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);

  ASSERT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

TEST(LlvmLibcPutcTest, ReturnConvertedCharacter) {
  constexpr char FILENAME[] = "testdata/putc_conversion.test";
  constexpr int inputs[] = {0, 0x7f, 0x80, 0xff, -1, -128, 0x100, 0x1ff};
  FILE *file = LIBC_NAMESPACE::fopen(FILENAME, "w");
  ASSERT_NE(file, nullptr);
  for (int c : inputs)
    EXPECT_EQ(LIBC_NAMESPACE::putc(c, file),
              static_cast<int>(static_cast<unsigned char>(c)));
  ASSERT_EQ(LIBC_NAMESPACE::fclose(file), 0);

  file = LIBC_NAMESPACE::fopen(FILENAME, "r");
  ASSERT_NE(file, nullptr);
  unsigned char bytes[sizeof(inputs) / sizeof(inputs[0])];
  ASSERT_EQ(LIBC_NAMESPACE::fread(bytes, 1, sizeof(bytes), file), sizeof(bytes));
  for (size_t i = 0; i < sizeof(bytes); ++i)
    EXPECT_EQ(bytes[i], static_cast<unsigned char>(inputs[i]));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}
