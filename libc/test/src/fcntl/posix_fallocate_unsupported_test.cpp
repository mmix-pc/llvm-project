//===-- Unsupported allocation tests for posix_fallocate --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/fcntl/posix_fallocate.h"
#include "src/sys/stat/fstat.h"
#include "src/unistd/close.h"
#include "src/unistd/lseek.h"
#include "src/unistd/pread.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

#include <sys/syscall.h>

namespace {
int syscall_error;
int syscall_calls;
int expected_fd;
off_t expected_offset;
off_t expected_length;
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T unsupported_fallocate_syscall(long number, int fd, int mode, off_t offset,
                               off_t length) {
  EXPECT_EQ(number, long(SYS_fallocate));
  EXPECT_EQ(fd, expected_fd);
  EXPECT_EQ(mode, 0);
  EXPECT_EQ(offset, expected_offset);
  EXPECT_EQ(length, expected_length);
  ++syscall_calls;
  return static_cast<T>(-syscall_error);
}
} // namespace LIBC_NAMESPACE_DECL

// Inject only the allocation syscall into the actual provider. File operations
// remain real; this checks fallback policy without requiring a ramfs mount.
#define syscall_impl unsupported_fallocate_syscall
#define posix_fallocate unsupported_posix_fallocate
#include "src/fcntl/linux/posix_fallocate.cpp"
#undef posix_fallocate
#undef syscall_impl

class LlvmLibcPosixFallocateUnsupportedTest
    : public LIBC_NAMESPACE::testing::Test {
protected:
  int fd = -1;

  void TearDown() override {
    if (fd >= 0) {
      EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
      EXPECT_EQ(LIBC_NAMESPACE::unlink(libc_make_test_file_path(
                    "posix_fallocate_unsupported.test")),
                0);
    }
  }
};

TEST_F(LlvmLibcPosixFallocateUnsupportedTest, NoFallbackWritesOrSizeChange) {
  fd = LIBC_NAMESPACE::open(
      libc_make_test_file_path("posix_fallocate_unsupported.test"),
      O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
  ASSERT_GE(fd, 0);
  const char original[] = "unsupported allocation must preserve this";
  ASSERT_EQ(LIBC_NAMESPACE::write(fd, original, sizeof(original)),
            ssize_t(sizeof(original)));
  ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 7, SEEK_SET), off_t(7));
  struct stat before;
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &before), 0);

  const off_t ranges[][2] = {{1, 7}, {0, 16384}, {16384, 4096}};
  const int errors[] = {EOPNOTSUPP, ENOSYS};
  const int saved_errnos[] = {0, EDOM};
  expected_fd = fd;
  for (int error : errors) {
    for (const auto &range : ranges) {
      for (int saved_errno : saved_errnos) {
        syscall_error = error;
        syscall_calls = 0;
        expected_offset = range[0];
        expected_length = range[1];
        LIBC_NAMESPACE::libc_errno = saved_errno;
        EXPECT_EQ(LIBC_NAMESPACE::unsupported_posix_fallocate(
                      fd, range[0], range[1]),
                  error);
        EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
        EXPECT_EQ(syscall_calls, 1);
        EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(7));
        struct stat after;
        ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &after), 0);
        EXPECT_EQ(after.st_size, before.st_size);
        EXPECT_EQ(after.st_blocks, before.st_blocks);
        char contents[sizeof(original)];
        ASSERT_EQ(LIBC_NAMESPACE::pread(fd, contents, sizeof(contents), 0),
                  ssize_t(sizeof(contents)));
        for (size_t i = 0; i < sizeof(contents); ++i)
          EXPECT_EQ(contents[i], original[i]);
      }
    }
  }
}
