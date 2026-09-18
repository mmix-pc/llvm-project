//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/limits.h"
#include "src/__support/arg_list.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/printf_core/error_mapper.h"
#include "src/__support/printf_core/printf_main.h"
#include "src/__support/printf_core/writer.h"
#include "src/stdio/vdprintf.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

static char output[4096];
static size_t used;
static unsigned calls, fail_at;
static int error;
namespace LIBC_NAMESPACE_DECL {
ssize_t test_write(int fd, const void *buffer, size_t size) {
  ++calls;
  if (fd != 7)
    __builtin_trap();
  if (calls == fail_at) {
    libc_errno = error;
    return error ? -1 : 0;
  }
  size_t n = size > 3 ? 3 : size;
  if (used + n > sizeof(output))
    __builtin_trap();
  for (size_t i = 0; i < n; ++i)
    output[used++] = static_cast<const char *>(buffer)[i];
  return static_cast<ssize_t>(n);
}
} // namespace LIBC_NAMESPACE_DECL

#define write test_write
#include "src/stdio/vdprintf.cpp"
#undef write

static int format(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = LIBC_NAMESPACE::vdprintf(7, fmt, args);
  va_end(args);
  return result;
}

TEST(LlvmLibcVDPrintfWriteTest, ShortWritesAndFlushes) {
  LIBC_NAMESPACE::libc_errno = EDOM;
  calls = 0;
  used = 0;
  fail_at = 0;
  EXPECT_EQ(format("%2048s", "x"), 2048);
  EXPECT_EQ(used, size_t(2048));
  EXPECT_GT(calls, 2U);
  for (size_t i = 0; i < 2047; ++i)
    EXPECT_EQ(output[i], ' ');
  EXPECT_EQ(output[2047], 'x');
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}

TEST(LlvmLibcVDPrintfWriteTest, ErrorsDoNotSpin) {
  const int errors[] = {0, EINTR, EAGAIN, EBADF};
  for (int e : errors) {
    calls = 0;
    used = 0;
    fail_at = 2;
    error = e;
    EXPECT_EQ(format("%2048s", "x"), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), e ? e : EIO);
    EXPECT_EQ(calls, 2U);
    EXPECT_EQ(used, size_t(3));
  }
}
