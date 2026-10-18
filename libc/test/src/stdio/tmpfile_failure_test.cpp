//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/File/file.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/tmpfile.h"
#include "test/UnitTest/Test.h"

namespace {
int open_error, close_count;
}
namespace LIBC_NAMESPACE_DECL {
class LinuxFile;
namespace linux_syscalls {
ErrorOr<int> test_open(const char *, int flags, mode_t) {
  EXPECT_EQ(flags, O_RDWR | O_TMPFILE | O_EXCL);
  return open_error ? ErrorOr<int>(Error(open_error)) : ErrorOr<int>(42);
}
ErrorOr<int> test_close(int fd) {
  EXPECT_EQ(fd, 42);
  ++close_count;
  libc_errno = EBADF;
  return Error(EINTR);
}
} // namespace linux_syscalls
ErrorOr<LinuxFile *> test_create_file_from_fd(int fd, const char *) {
  EXPECT_EQ(fd, 42);
  return Error(ENOMEM);
}
} // namespace LIBC_NAMESPACE_DECL

// Exercise production failure paths without replacing the test runner's I/O.
#define open(...) test_open(__VA_ARGS__)
#define close(...) test_close(__VA_ARGS__)
#define create_file_from_fd test_create_file_from_fd
#define tmpfile injected_tmpfile
#include "src/stdio/linux/tmpfile.cpp"
#undef tmpfile
#undef create_file_from_fd
#undef close
#undef open

TEST(LlvmLibcTmpfileFailureTest, StreamConstructionClosesOnce) {
  open_error = 0;
  close_count = 0;
  EXPECT_EQ(LIBC_NAMESPACE::injected_tmpfile(), nullptr);
  EXPECT_EQ(close_count, 1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  open_error = EACCES;
  close_count = 0;
  EXPECT_EQ(LIBC_NAMESPACE::injected_tmpfile(), nullptr);
  EXPECT_EQ(close_count, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EACCES);
}
