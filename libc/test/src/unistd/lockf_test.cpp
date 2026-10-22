//===-- Unittests for lockf -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/unistd_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/unistd/close.h"
#include "src/unistd/lockf.h"
#include "src/unistd/lseek.h"
#include "src/unistd/unlink.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcLockfTest, InvalidDescriptor) {
  const int commands[] = {F_ULOCK, F_LOCK, F_TLOCK, F_TEST};
  for (int cmd : commands) {
    LIBC_NAMESPACE::libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::lockf(-1, cmd, 0), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  }
}

TEST(LlvmLibcLockfTest, InvalidCommand) {
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::lockf(-1, -1, 0), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
}

class LlvmLibcLockfFileTest : public LIBC_NAMESPACE::testing::Test {
protected:
  int fd = -1;

  void TearDown() override {
    if (fd >= 0) {
      EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
      EXPECT_EQ(LIBC_NAMESPACE::unlink(
                    libc_make_test_file_path("testdata/lockf.test")),
                0);
    }
  }
};

TEST_F(LlvmLibcLockfFileTest, CurrentPositionAndRangeErrors) {
  fd = LIBC_NAMESPACE::open(libc_make_test_file_path("testdata/lockf.test"),
                            O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 16, SEEK_SET), off_t(16));

  // This is a local range/error check, not evidence of interprocess exclusion.
  const off_t lengths[] = {8, -8, 0};
  for (off_t len : lengths) {
    EXPECT_EQ(LIBC_NAMESPACE::lockf(fd, F_TEST, len), 0);
    EXPECT_EQ(LIBC_NAMESPACE::lockf(fd, F_LOCK, len), 0);
    EXPECT_EQ(LIBC_NAMESPACE::lockf(fd, F_TLOCK, len), 0);
    EXPECT_EQ(LIBC_NAMESPACE::lockf(fd, F_TEST, len), 0);
    EXPECT_EQ(LIBC_NAMESPACE::lockf(fd, F_ULOCK, len), 0);
    EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(16));
  }

  const int commands[] = {F_ULOCK, F_LOCK, F_TLOCK, F_TEST};
  for (int cmd : commands) {
    LIBC_NAMESPACE::libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::lockf(fd, cmd, -17), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
    EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(16));
  }
}
