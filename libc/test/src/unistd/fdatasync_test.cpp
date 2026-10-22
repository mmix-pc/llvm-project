//===-- Unittests for fdatasync -------------------------------------------===//
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
#include "hdr/unistd_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/sys/stat/fstat.h"
#include "src/unistd/close.h"
#include "src/unistd/fdatasync.h"
#include "src/unistd/lseek.h"
#include "src/unistd/pipe.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

class LlvmLibcFdatasyncTest : public LIBC_NAMESPACE::testing::Test {
protected:
  int fd = -1;

  void TearDown() override {
    if (fd >= 0) {
      EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
      EXPECT_EQ(
          LIBC_NAMESPACE::unlink(libc_make_test_file_path("fdatasync.test")),
          0);
    }
  }
};

TEST_F(LlvmLibcFdatasyncTest, WrittenFilePreservesSizeOffsetAndErrno) {
  fd = LIBC_NAMESPACE::open(libc_make_test_file_path("fdatasync.test"),
                            O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
  ASSERT_GE(fd, 0);
  const char data[] = "fdatasync data";
  ASSERT_EQ(LIBC_NAMESPACE::write(fd, data, sizeof(data)),
            ssize_t(sizeof(data)));
  ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 3, SEEK_SET), off_t(3));
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::fdatasync(fd), 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR), off_t(3));
  struct stat st;
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &st), 0);
  EXPECT_EQ(st.st_size, off_t(sizeof(data)));
  // Successful submission and unchanged metadata do not prove disk durability.
}

TEST(LlvmLibcFdatasyncErrorTest, InvalidDescriptorAndPipe) {
  EXPECT_EQ(LIBC_NAMESPACE::fdatasync(-1), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  int fds[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe(fds), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fdatasync(fds[0]), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::fdatasync(fds[1]), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::close(fds[0]), 0);
  EXPECT_EQ(LIBC_NAMESPACE::close(fds[1]), 0);
}
