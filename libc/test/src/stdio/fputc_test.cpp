//===-- Unittests for fputc / putchar -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/File/file.h"
#include "src/stdio/fclose.h"
#include "src/stdio/ferror.h"
#include "src/stdio/fopen.h"
#include "src/stdio/fputc.h"
#include "src/stdio/fread.h"
#include "src/stdio/putchar.h"
#include "src/stdio/stderr.h"
#include "src/stdio/stdout.h"

#include "test/UnitTest/Test.h"

TEST(LlvmLibcPutcTest, PrintOut) {
  int result;

  constexpr char simple[] = "A simple string written to stdout\n";
  for (const char &c : simple) {
    if (!c)
      break;
    result = LIBC_NAMESPACE::putchar(c);
    EXPECT_EQ(result, static_cast<int>(static_cast<unsigned char>(c)));
  }

  constexpr char more[] = "A simple string written to stderr\n";
  for (const char &c : more) {
    if (!c)
      break;
    result = LIBC_NAMESPACE::fputc(
        c, reinterpret_cast<FILE *>(LIBC_NAMESPACE::stderr));
    EXPECT_EQ(result, static_cast<int>(static_cast<unsigned char>(c)));
  }
}

static int write_stdout(int c, FILE *stream) {
  FILE *saved = LIBC_NAMESPACE::stdout;
  LIBC_NAMESPACE::stdout = stream;
  int result = LIBC_NAMESPACE::putchar(c);
  LIBC_NAMESPACE::stdout = saved;
  return result;
}

TEST(LlvmLibcPutcTest, ReturnConvertedCharacter) {
  constexpr char FILENAME[] = "testdata/fputc_output.test";
  constexpr int inputs[] = {'x', 0, 0x7f, 0x80, 0xff, -1, -128, 0x100, 0x1ff};
  int (*writers[])(int, FILE *) = {LIBC_NAMESPACE::fputc, write_stdout};
  for (auto write : writers) {
    FILE *file = LIBC_NAMESPACE::fopen(FILENAME, "w");
    ASSERT_NE(file, nullptr);
    for (int c : inputs)
      EXPECT_EQ(write(c, file), static_cast<int>(static_cast<unsigned char>(c)));
    ASSERT_EQ(LIBC_NAMESPACE::fclose(file), 0);

    file = LIBC_NAMESPACE::fopen(FILENAME, "r");
    ASSERT_NE(file, nullptr);
    unsigned char bytes[sizeof(inputs) / sizeof(inputs[0])];
    ASSERT_EQ(LIBC_NAMESPACE::fread(bytes, 1, sizeof(bytes), file), sizeof(bytes));
    for (size_t i = 0; i < sizeof(bytes); ++i)
      EXPECT_EQ(bytes[i], static_cast<unsigned char>(inputs[i]));
    EXPECT_EQ(write('x', file), EOF);
    EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);
    EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
  }
}
