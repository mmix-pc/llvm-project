//===-- Native filesystem tests for posix_fallocate -----------------------===//
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
#include "src/__support/CPP/limits.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/fcntl/posix_fallocate.h"
#include "src/sys/stat/fstat.h"
#include "src/unistd/close.h"
#include "src/unistd/lseek.h"
#include "src/unistd/pipe.h"
#include "src/unistd/pread.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

// Run on a disposable tmpfs/ext4 test directory with available space. An
// unsupported backend must fail the positive test, not silently skip it.
class LlvmLibcPosixFallocateTest : public LIBC_NAMESPACE::testing::Test {
protected:
  int fd = -1;
  int readonly_fd = -1;

  int createFile() {
    fd = LIBC_NAMESPACE::open(libc_make_test_file_path("posix_fallocate.test"),
                              O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    return fd;
  }

  void TearDown() override {
    if (readonly_fd >= 0)
      EXPECT_EQ(LIBC_NAMESPACE::close(readonly_fd), 0);
    if (fd >= 0) {
      EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
      EXPECT_EQ(LIBC_NAMESPACE::unlink(
                    libc_make_test_file_path("posix_fallocate.test")),
                0);
    }
  }
};

TEST_F(LlvmLibcPosixFallocateTest, AllocatesBlocksAndPreservesDataAndPosition) {
  ASSERT_GE(createFile(), 0);
  char original[4096];
  for (size_t i = 0; i < sizeof(original); ++i)
    original[i] = static_cast<char>(1 + i % 113);
  ASSERT_EQ(LIBC_NAMESPACE::write(fd, original, sizeof(original)),
            ssize_t(sizeof(original)));
  ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 23, SEEK_SET), off_t(23));

  constexpr off_t END = 16384;
  LIBC_NAMESPACE::libc_errno = EDOM;
  int (*allocate)(int, off_t, off_t) = LIBC_NAMESPACE::posix_fallocate;
  int result = allocate(fd, 17, END - 17);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  ASSERT_EQ(result, 0);
  EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(23));
  struct stat st;
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &st), 0);
  EXPECT_EQ(st.st_size, END);
  EXPECT_GE(st.st_blocks, decltype(st.st_blocks)(END / 512));

  // Allocating wholly inside the file must not shrink it.
  LIBC_NAMESPACE::libc_errno = ERANGE;
  result = LIBC_NAMESPACE::posix_fallocate(fd, 64, 32);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  ASSERT_EQ(result, 0);
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &st), 0);
  EXPECT_EQ(st.st_size, END);
  char contents[END];
  ASSERT_EQ(LIBC_NAMESPACE::pread(fd, contents, sizeof(contents), 0),
            ssize_t(sizeof(contents)));
  for (size_t i = 0; i < sizeof(original); ++i)
    EXPECT_EQ(contents[i], original[i]);
  for (size_t i = sizeof(original); i < sizeof(contents); ++i)
    EXPECT_EQ(contents[i], char(0));
  EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(23));
  // Block accounting is stronger than size alone, but does not qualify the
  // reservation guarantee under ENOSPC, quota pressure or concurrent writes.
}

TEST_F(LlvmLibcPosixFallocateTest, InvalidRangesAndReadonlyFilePreserveState) {
  ASSERT_GE(createFile(), 0);
  const char original[] = "preserve allocation failure data";
  ASSERT_EQ(LIBC_NAMESPACE::write(fd, original, sizeof(original)),
            ssize_t(sizeof(original)));
  ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 3, SEEK_SET), off_t(3));
  struct stat before;
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &before), 0);
  constexpr off_t max = LIBC_NAMESPACE::cpp::numeric_limits<off_t>::max();
  struct Case {
    off_t offset, len;
    int error;
  };
  const Case cases[] = {{-1, 1, EINVAL},
                        {0, 0, EINVAL},
                        {0, -1, EINVAL},
                        {max, 1, EFBIG},
                        {max - 1, 2, EFBIG}};
  for (const auto &test : cases) {
    LIBC_NAMESPACE::libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::posix_fallocate(fd, test.offset, test.len),
              test.error);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  }
  readonly_fd = LIBC_NAMESPACE::open(
      libc_make_test_file_path("posix_fallocate.test"), O_RDONLY);
  ASSERT_GE(readonly_fd, 0);
  LIBC_NAMESPACE::libc_errno = ERANGE;
  EXPECT_EQ(LIBC_NAMESPACE::posix_fallocate(readonly_fd, 0, 8192), EBADF);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);

  struct stat after;
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &after), 0);
  EXPECT_EQ(after.st_size, before.st_size);
  EXPECT_EQ(after.st_blocks, before.st_blocks);
  char contents[sizeof(original)];
  ASSERT_EQ(LIBC_NAMESPACE::pread(fd, contents, sizeof(contents), 0),
            ssize_t(sizeof(contents)));
  for (size_t i = 0; i < sizeof(contents); ++i)
    EXPECT_EQ(contents[i], original[i]);
  EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(3));
}

TEST(LlvmLibcPosixFallocateErrorTest, InvalidDescriptorAndPipe) {
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::posix_fallocate(-1, 0, 4096), EBADF);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  int fds[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe(fds), 0);
  LIBC_NAMESPACE::libc_errno = ERANGE;
  EXPECT_EQ(LIBC_NAMESPACE::posix_fallocate(fds[1], 0, 4096), ESPIPE);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  EXPECT_EQ(LIBC_NAMESPACE::close(fds[0]), 0);
  EXPECT_EQ(LIBC_NAMESPACE::close(fds[1]), 0);
}
