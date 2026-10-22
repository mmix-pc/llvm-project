//===-- Native filesystem tests for posix_fadvise -------------------------===//
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
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/fcntl/posix_fadvise.h"
#include "src/sys/stat/fstat.h"
#include "src/unistd/close.h"
#include "src/unistd/lseek.h"
#include "src/unistd/pipe.h"
#include "src/unistd/pread.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

class LlvmLibcPosixFadviseTest : public LIBC_NAMESPACE::testing::Test {
protected:
  int fd = -1;

  int createFile() {
    fd = LIBC_NAMESPACE::open(libc_make_test_file_path("posix_fadvise.test"),
                              O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    return fd;
  }

  void TearDown() override {
    if (fd >= 0) {
      EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
      EXPECT_EQ(LIBC_NAMESPACE::unlink(
                    libc_make_test_file_path("posix_fadvise.test")),
                0);
    }
  }
};

TEST_F(LlvmLibcPosixFadviseTest, AllAdvicePreservesDataSizeAndPosition) {
  ASSERT_GE(createFile(), 0);
  char original[8192];
  for (size_t i = 0; i < sizeof(original); ++i)
    original[i] = static_cast<char>(1 + i % 113);
  ASSERT_EQ(LIBC_NAMESPACE::write(fd, original, sizeof(original)),
            ssize_t(sizeof(original)));
  ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 17, SEEK_SET), off_t(17));

  const int advice_values[] = {POSIX_FADV_NORMAL,     POSIX_FADV_RANDOM,
                               POSIX_FADV_SEQUENTIAL, POSIX_FADV_WILLNEED,
                               POSIX_FADV_DONTNEED,   POSIX_FADV_NOREUSE};
  int (*advise)(int, off_t, off_t, int) = LIBC_NAMESPACE::posix_fadvise;
  for (int advice : advice_values) {
    LIBC_NAMESPACE::libc_errno = EDOM;
    // Zero length means through EOF, not an invalid or empty range.
    EXPECT_EQ(advise(fd, 0, 0, advice), 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(17));
    struct stat st;
    ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &st), 0);
    EXPECT_EQ(st.st_size, off_t(sizeof(original)));
    char contents[sizeof(original)];
    ASSERT_EQ(LIBC_NAMESPACE::pread(fd, contents, sizeof(contents), 0),
              ssize_t(sizeof(contents)));
    for (size_t i = 0; i < sizeof(contents); ++i)
      EXPECT_EQ(contents[i], original[i]);
  }
  // No cache residency or performance assertion: hints may be ignored.
}

TEST_F(LlvmLibcPosixFadviseTest, ValidAndInvalidRangesAndAdvice) {
  ASSERT_GE(createFile(), 0);
  struct Case {
    off_t offset, len;
    int advice, error;
  };
  const Case cases[] = {{0, 0, POSIX_FADV_NORMAL, 0},
                        {3, 17, POSIX_FADV_WILLNEED, 0},
                        {(off_t(1) << 40) + 3, 17, POSIX_FADV_NORMAL, 0},
                        {0, -1, POSIX_FADV_NORMAL, EINVAL},
                        {0, 0, -1, EINVAL},
                        {0, 1, 0x7fffffff, EINVAL}};
  for (const auto &test : cases) {
    LIBC_NAMESPACE::libc_errno = ERANGE;
    EXPECT_EQ(
        LIBC_NAMESPACE::posix_fadvise(fd, test.offset, test.len, test.advice),
        test.error);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  }
  // POSIX requires EINVAL for a negative length, not a negative offset.
  // Linux backends differ on whether an ignored negative-offset hint fails.
  LIBC_NAMESPACE::libc_errno = EDOM;
  int negative_offset = LIBC_NAMESPACE::posix_fadvise(fd, -1, 1, POSIX_FADV_NORMAL);
  EXPECT_TRUE(negative_offset == 0 || negative_offset == EINVAL);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  struct stat st;
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &st), 0);
  EXPECT_EQ(st.st_size, off_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(0));
}

TEST(LlvmLibcPosixFadviseErrorTest, InvalidDescriptorAndPipe) {
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::posix_fadvise(-1, 0, 0, POSIX_FADV_NORMAL), EBADF);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  int fds[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe(fds), 0);
  for (int fd : fds) {
    LIBC_NAMESPACE::libc_errno = ERANGE;
    EXPECT_EQ(LIBC_NAMESPACE::posix_fadvise(fd, 0, 0, POSIX_FADV_NORMAL),
              ESPIPE);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
    EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
  }
}
