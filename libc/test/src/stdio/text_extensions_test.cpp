//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/dprintf.h"
#include "src/stdio/fdopen.h"
#include "src/stdio/fgets_unlocked.h"
#include "src/stdio/fileno_unlocked.h"
#include "src/stdio/fputc_unlocked.h"
#include "src/stdio/fputs_unlocked.h"
#include "src/stdio/getdelim.h"
#include "src/stdio/getline.h"
#include "src/stdio/putc_unlocked.h"
#include "src/stdio/remove.h"
#include "src/unistd/close.h"
#include "src/unistd/pipe.h"
#include "src/unistd/read.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

static bool fail_allocation;
static void *line_realloc(void *p, size_t n) {
  return fail_allocation ? nullptr : ::realloc(p, n);
}
#define realloc line_realloc
#include "src/stdio/generic/getdelim.cpp"
#undef realloc

using namespace LIBC_NAMESPACE;

TEST(LlvmLibcTextExtensionsTest, LinesGrowthBinaryAndEOF) {
  const char *path = libc_make_test_file_path("text-lines.test");
  auto f = openfile(path, "w+");
  ASSERT_TRUE(f.has_value());
  const char prefix[] = {'a', '\0', 'b', '\n'};
  ASSERT_EQ(f.value()->write(prefix, sizeof(prefix)).value, sizeof(prefix));
  char block[4096];
  for (char &c : block)
    c = 'x';
  ASSERT_EQ(f.value()->write(block, sizeof(block)).value, sizeof(block));
  ASSERT_TRUE(f.value()->seek(0, SEEK_SET).has_value());
  auto *stream = reinterpret_cast<::FILE *>(f.value());
  char *line = nullptr;
  size_t capacity = 999;
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(4));
  ASSERT_TRUE(line != nullptr);
  EXPECT_EQ(line[1], '\0');
  EXPECT_EQ(line[3], '\n');
  EXPECT_EQ(line[4], '\0');
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(4096));
  EXPECT_GE(capacity, size_t(4097));
  EXPECT_EQ(line[4095], 'x');
  EXPECT_EQ(line[4096], '\0');
  libc_errno = EDOM;
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(-1));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_TRUE(f.value()->iseof());
  ::free(line);
  EXPECT_EQ(f.value()->close(), 0);
  EXPECT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcTextExtensionsTest, DelimiterAndAllocationFailure) {
  const char *path = libc_make_test_file_path("text-delimiters.test");
  auto f = openfile(path, "w+");
  ASSERT_TRUE(f.has_value());
  const char data[] = "first:second\n";
  ASSERT_EQ(f.value()->write(data, sizeof(data) - 1).value, sizeof(data) - 1);
  ASSERT_TRUE(f.value()->seek(0, SEEK_SET).has_value());
  auto *stream = reinterpret_cast<::FILE *>(f.value());
  char *line = nullptr;
  size_t capacity = 0;
  fail_allocation = true;
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(-1));
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_TRUE(f.value()->error());
  EXPECT_EQ(line, static_cast<char *>(nullptr));
  fail_allocation = false;
  f.value()->clearerr();
  EXPECT_EQ(getdelim(&line, &capacity, ':', stream), ssize_t(6));
  EXPECT_STREQ(line, "first:");
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(7));
  EXPECT_STREQ(line, "second\n");
  ASSERT_TRUE(f.value()->seek(0, SEEK_SET).has_value());
  char *small = static_cast<char *>(::realloc(line, 2));
  ASSERT_TRUE(small != nullptr);
  line = small;
  capacity = 2;
  fail_allocation = true;
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(-1));
  EXPECT_EQ(line, small);
  EXPECT_EQ(capacity, size_t(2));
  EXPECT_STREQ(line, "f");
  EXPECT_TRUE(f.value()->error());
  fail_allocation = false;
  f.value()->clearerr();
  ::free(line);
  EXPECT_EQ(f.value()->close(), 0);
  EXPECT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcTextExtensionsTest, UnlockedCharactersAndLines) {
  const char *path = libc_make_test_file_path("text-unlocked.test");
  auto f = openfile(path, "w+");
  ASSERT_TRUE(f.has_value());
  auto *stream = reinterpret_cast<::FILE *>(f.value());
  EXPECT_EQ(fputc_unlocked(0x1ff, stream), 255);
  EXPECT_EQ(putc_unlocked('\n', stream), int('\n'));
  EXPECT_GE(fputs_unlocked("abc\nlast", stream), 0);
  EXPECT_GE(fileno_unlocked(stream), 0);
  ASSERT_TRUE(f.value()->seek(0, SEEK_SET).has_value());
  char buffer[16];
  EXPECT_EQ(fgets_unlocked(buffer, 1, stream), buffer);
  EXPECT_EQ(buffer[0], '\0');
  EXPECT_EQ(fgets_unlocked(buffer, 0, stream), static_cast<char *>(nullptr));
  EXPECT_EQ(fgets_unlocked(buffer, 16, stream), buffer);
  EXPECT_EQ(static_cast<unsigned char>(buffer[0]), (unsigned char)255);
  EXPECT_EQ(fgets_unlocked(buffer, 16, stream), buffer);
  EXPECT_STREQ(buffer, "abc\n");
  EXPECT_EQ(fgets_unlocked(buffer, 16, stream), buffer);
  EXPECT_STREQ(buffer, "last");
  EXPECT_EQ(fgets_unlocked(buffer, 16, stream), static_cast<char *>(nullptr));
  EXPECT_EQ(fputc_unlocked('!', stream), int('!'));
  EXPECT_EQ(f.value()->close(), 0);
  EXPECT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcTextExtensionsTest, DescriptorFormatting) {
  int fd[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe(fd), 0);
  EXPECT_EQ(dprintf(fd[1], "%s:%04d:%c", "value", 7, 0), 12);
  char buffer[32]{};
  EXPECT_EQ(LIBC_NAMESPACE::read(fd[0], buffer, sizeof(buffer)), ssize_t(12));
  EXPECT_STREQ(buffer, "value:0007:");
  EXPECT_EQ(buffer[11], '\0');
  EXPECT_EQ(dprintf(-1, "%s", "invalid"), -1);
  EXPECT_EQ(int(libc_errno), EBADF);
  EXPECT_EQ(LIBC_NAMESPACE::close(fd[0]), 0);
  EXPECT_EQ(LIBC_NAMESPACE::close(fd[1]), 0);
}

TEST(LlvmLibcTextExtensionsTest, InvalidInputAndReadErrors) {
  char *line = nullptr;
  size_t capacity = 0;
  EXPECT_EQ(getdelim(&line, &capacity, '\n', nullptr), ssize_t(-1));
  EXPECT_EQ(int(libc_errno), EINVAL);
  const char *path = libc_make_test_file_path("text-read-error.test");
  auto f = openfile(path, "w");
  ASSERT_TRUE(f.has_value());
  auto *stream = reinterpret_cast<::FILE *>(f.value());
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(-1));
  EXPECT_EQ(int(libc_errno), EBADF);
  EXPECT_TRUE(f.value()->error());
  f.value()->clearerr();
  char buffer[4];
  EXPECT_EQ(fgets_unlocked(buffer, sizeof(buffer), stream),
            static_cast<char *>(nullptr));
  EXPECT_EQ(int(libc_errno), EBADF);
  f.value()->clearerr();
  EXPECT_EQ(getline(nullptr, &capacity, stream), ssize_t(-1));
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_TRUE(f.value()->error());
  ::free(line);
  EXPECT_EQ(f.value()->close(), 0);
  EXPECT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcTextExtensionsTest, NonseekableAndNulDelimiter) {
  int fd[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe(fd), 0);
  const char data[] = {'a', '\0', '\n'};
  ASSERT_EQ(LIBC_NAMESPACE::write(fd[1], data, sizeof(data)), ssize_t(3));
  ASSERT_EQ(LIBC_NAMESPACE::close(fd[1]), 0);
  auto *stream = LIBC_NAMESPACE::fdopen(fd[0], "r");
  ASSERT_TRUE(stream != nullptr);
  char *line = nullptr;
  size_t capacity = 0;
  EXPECT_EQ(getdelim(&line, &capacity, 0, stream), ssize_t(2));
  EXPECT_EQ(line[0], 'a');
  EXPECT_EQ(line[1], '\0');
  EXPECT_EQ(line[2], '\0');
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(1));
  EXPECT_STREQ(line, "\n");
  EXPECT_EQ(getline(&line, &capacity, stream), ssize_t(-1));
  ::free(line);
  EXPECT_EQ(reinterpret_cast<File *>(stream)->close(), 0);
}
