//===-- Tests for mkostemp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/stdlib/mkostemp.h"
#include "src/sys/stat/fstat.h"
#include "src/unistd/close.h"
#include "src/unistd/lseek.h"
#include "src/unistd/pread.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcMkostempTest, FlagsPermissionsAndAppendSemantics) {
  const int flags[] = {0, O_CLOEXEC, O_APPEND, O_APPEND | O_CLOEXEC,
                       O_DSYNC, O_SYNC, O_RSYNC};
  for (int flag : flags) {
    char path[] = "mkostemp-XXXXXX";
    int fd = LIBC_NAMESPACE::mkostemp(path, flag);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(fd, F_GETFD) & FD_CLOEXEC,
              (flag & O_CLOEXEC) ? FD_CLOEXEC : 0);
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(fd, F_GETFL) & O_ACCMODE, O_RDWR);
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(fd, F_GETFL) & flag & ~O_CLOEXEC,
              flag & ~O_CLOEXEC);
    struct stat status;
    ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &status), 0);
    EXPECT_EQ(status.st_mode & mode_t(0777), mode_t(0600));
    ASSERT_EQ(LIBC_NAMESPACE::write(fd, "a", 1), ssize_t(1));
    ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_SET), off_t(0));
    ASSERT_EQ(LIBC_NAMESPACE::write(fd, "b", 1), ssize_t(1));
    char bytes[2]{};
    EXPECT_EQ(LIBC_NAMESPACE::pread(fd, bytes, 2, 0),
              ssize_t((flag & O_APPEND) ? 2 : 1));
    EXPECT_EQ(bytes[0], (flag & O_APPEND) ? 'a' : 'b');
    if (flag & O_APPEND)
      EXPECT_EQ(bytes[1], 'b');
    EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
    EXPECT_EQ(LIBC_NAMESPACE::unlink(path), 0);
  }
}

TEST(LlvmLibcMkostempTest, RejectsInvalidFlagsAndTemplates) {
  char path[] = "mkostemp-XXXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, O_TRUNC), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_STREQ(path, "mkostemp-XXXXXX");
  char invalid[] = "mkostemp-XXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::mkostemp(invalid, O_CLOEXEC), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_STREQ(invalid, "mkostemp-XXXXX");
}
