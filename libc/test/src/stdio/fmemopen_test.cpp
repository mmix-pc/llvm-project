//===-- Tests for fixed-size memory streams -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/stdio/clearerr.h"
#include "src/stdio/fclose.h"
#include "src/stdio/feof.h"
#include "src/stdio/ferror.h"
#include "src/stdio/fflush.h"
#include "src/stdio/fgetc.h"
#include "src/stdio/fgets.h"
#include "src/stdio/fileno.h"
#include "src/stdio/fmemopen.h"
#include "src/stdio/fputc.h"
#include "src/stdio/fputs.h"
#include "src/stdio/fread.h"
#include "src/stdio/fseek.h"
#include "src/stdio/ftell.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/setvbuf.h"
#include "test/UnitTest/ErrnoCheckingTest.h"

using LlvmLibcFmemopenTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;
namespace LIBC = LIBC_NAMESPACE;

TEST_F(LlvmLibcFmemopenTest, ReadIncludesNulsAndEofPreservesDestination) {
  char memory[] = "test";
  FILE *f = LIBC::fmemopen(memory, sizeof(memory), "r");
  ASSERT_NE(f, nullptr);
  char result[10];
  ASSERT_EQ(LIBC::fgets(result, sizeof(result), f), result);
  EXPECT_STREQ(result, "test");
  EXPECT_EQ(LIBC::ftell(f), 5L);
  ASSERT_EQ(LIBC::fgets(result, sizeof(result), f), nullptr);
  EXPECT_STREQ(result, "test");
  EXPECT_NE(LIBC::feof(f), 0);
  LIBC::clearerr(f);
  EXPECT_EQ(LIBC::feof(f), 0);
  EXPECT_EQ(LIBC::fwrite("x", 1, 1, f), size_t(0));
  ASSERT_ERRNO_EQ(EBADF);
  EXPECT_NE(LIBC::ferror(f), 0);
  EXPECT_STREQ(memory, "test");
  EXPECT_EQ(LIBC::fclose(f), 0);
}

TEST_F(LlvmLibcFmemopenTest, UpdateAppendAndPositionPastEnd) {
  char memory[10] = {};
  FILE *f = LIBC::fmemopen(memory, sizeof(memory), "r+");
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fputs("hello", f), 0);
  ASSERT_EQ(LIBC::fputc(0, f), 0);
  ASSERT_EQ(LIBC::fseek(f, 0, SEEK_SET), 0);
  char text[5];
  ASSERT_EQ(LIBC::fread(text, 1, sizeof(text), f), sizeof(text));
  EXPECT_EQ(LIBC::ftell(f), 5L);
  EXPECT_EQ(LIBC::fseek(f, 6, SEEK_CUR), -1);
  ASSERT_ERRNO_EQ(EINVAL);
  EXPECT_EQ(LIBC::ftell(f), 5L);
  EXPECT_STREQ(memory, "hello");
  ASSERT_EQ(LIBC::fclose(f), 0);

  f = LIBC::fmemopen(memory, sizeof(memory), "a+");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(LIBC::ftell(f), 5L);
  ASSERT_EQ(LIBC::fseek(f, 6, SEEK_SET), 0);
  EXPECT_EQ(LIBC::fgetc(f), EOF);
  EXPECT_EQ(LIBC::ftell(f), 6L);
  ASSERT_EQ(LIBC::fseek(f, 0, SEEK_SET), 0);
  EXPECT_EQ(LIBC::fgetc(f), int('h'));
  ASSERT_EQ(LIBC::fseek(f, 0, SEEK_CUR), 0);
  ASSERT_EQ(LIBC::fwrite("104", 1, 3, f), size_t(3));
  EXPECT_EQ(LIBC::ftell(f), 8L);
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(LIBC::ftell(f), 8L);
  EXPECT_STREQ(memory, "hello104");
  EXPECT_EQ(LIBC::fclose(f), 0);
}

TEST_F(LlvmLibcFmemopenTest, TruncateSeekGapAndExternalOwnership) {
  char memory[10] = "previous";
  FILE *f = LIBC::fmemopen(memory, sizeof(memory), "w+b");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(memory[0], '\0');
  ASSERT_EQ(LIBC::fseek(f, 3, SEEK_SET), 0);
  ASSERT_EQ(LIBC::fputc('q', f), int('q'));
  ASSERT_EQ(LIBC::fclose(f), 0);
  for (unsigned i = 0; i != 3; ++i)
    EXPECT_EQ(memory[i], '\0');
  EXPECT_EQ(memory[3], 'q');
  EXPECT_EQ(memory[4], '\0');
  memory[0] = 'x'; // Closing never frees caller-owned storage.
}

TEST_F(LlvmLibcFmemopenTest, FullBufferAndCloseAfterFlushFailure) {
  char guarded[] = "!....!";
  FILE *f = LIBC::fmemopen(guarded + 1, 4, "w");
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::setvbuf(f, nullptr, _IONBF, 0), 0);
  EXPECT_EQ(LIBC::fwrite("abcde", 1, 5, f), size_t(4));
  ASSERT_ERRNO_EQ(ENOSPC);
  EXPECT_NE(LIBC::ferror(f), 0);
  EXPECT_STREQ(guarded + 1, "abc");
  EXPECT_EQ(guarded[0], '!');
  EXPECT_EQ(guarded[5], '!');
  EXPECT_EQ(LIBC::fclose(f), 0);

  f = LIBC::fmemopen(guarded + 1, 4, "w");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(LIBC::fwrite("abcde", 1, 5, f), size_t(5));
  EXPECT_EQ(LIBC::fclose(f), EOF);
  ASSERT_ERRNO_EQ(ENOSPC);
  // A failed close must remove the stream from global flush traversal.
  EXPECT_EQ(LIBC::fflush(nullptr), 0);
}

TEST_F(LlvmLibcFmemopenTest, OwnedAndZeroLengthBuffers) {
  FILE *f = LIBC::fmemopen(nullptr, 8, "w+");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(LIBC::fileno(f), -1);
  ASSERT_ERRNO_EQ(EBADF);
  ASSERT_EQ(LIBC::fputs("abc", f), 0);
  ASSERT_EQ(LIBC::fseek(f, 0, SEEK_SET), 0);
  char text[8];
  ASSERT_EQ(LIBC::fgets(text, sizeof(text), f), text);
  EXPECT_STREQ(text, "abc");
  ASSERT_EQ(LIBC::fclose(f), 0);

  char guard = '!';
  f = LIBC::fmemopen(&guard, 0, "w+");
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::setvbuf(f, nullptr, _IONBF, 0), 0);
  EXPECT_EQ(LIBC::fputc('x', f), EOF);
  ASSERT_ERRNO_EQ(ENOSPC);
  EXPECT_EQ(guard, '!');
  EXPECT_EQ(LIBC::fclose(f), 0);
}

TEST_F(LlvmLibcFmemopenTest, LineBufferedErrorsKeepErrno) {
  char memory[2];
  char staging[8];
  FILE *f = LIBC::fmemopen(memory, sizeof(memory), "w");
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::setvbuf(f, staging, _IOLBF, sizeof(staging)), 0);
  EXPECT_EQ(LIBC::fwrite("abc\n", 1, 4, f), size_t(2));
  ASSERT_ERRNO_EQ(ENOSPC);
  EXPECT_NE(LIBC::ferror(f), 0);
  EXPECT_EQ(memory[0], 'a');
  EXPECT_EQ(memory[1], '\0');
  EXPECT_EQ(LIBC::fclose(f), 0);
}

TEST_F(LlvmLibcFmemopenTest, InvalidModeDoesNotModifyBuffer) {
  char memory[] = "keep";
  EXPECT_EQ(LIBC::fmemopen(memory, sizeof(memory), "rw"), nullptr);
  ASSERT_ERRNO_EQ(EINVAL);
  EXPECT_STREQ(memory, "keep");
  EXPECT_EQ(LIBC::fmemopen(memory, sizeof(memory), ""), nullptr);
  ASSERT_ERRNO_EQ(EINVAL);
}

TEST_F(LlvmLibcFmemopenTest, AppendToBufferWithoutTerminator) {
  char memory[] = {'a', 'b', 'c'};
  FILE *f = LIBC::fmemopen(memory, sizeof(memory), "a+");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(LIBC::ftell(f), 3L);
  ASSERT_EQ(LIBC::setvbuf(f, nullptr, _IONBF, 0), 0);
  EXPECT_EQ(LIBC::fputc('x', f), EOF);
  ASSERT_ERRNO_EQ(ENOSPC);
  ASSERT_EQ(LIBC::fseek(f, 0, SEEK_SET), 0);
  EXPECT_EQ(LIBC::fgetc(f), int('a'));
  EXPECT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(memory[2], 'c');
}
