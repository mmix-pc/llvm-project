//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/sys/stat/fstat.h"
#include "src/sys/stat/futimens.h"
#include "src/unistd/close.h"
#include "src/unistd/unlink.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcMMIXFutimensTest, DescriptorAndErrors) {
  const char *path = libc_make_test_file_path("mmix-futimens.test");
  int fd = LIBC_NAMESPACE::open(path, O_CREAT | O_RDWR, S_IRWXU);
  ASSERT_GE(fd, 0);
  // The descriptor remains usable after unlink; no pathname lookup is possible.
  ASSERT_EQ(LIBC_NAMESPACE::unlink(path), 0);
  timespec times[2] = {{54321, 12345}, {43210, 23456}};
  ASSERT_EQ(LIBC_NAMESPACE::futimens(fd, times), 0);
  struct stat info;
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &info), 0);
  EXPECT_EQ(info.st_atim.tv_sec, times[0].tv_sec);
  EXPECT_EQ(info.st_atim.tv_nsec, times[0].tv_nsec);
  EXPECT_EQ(info.st_mtim.tv_sec, times[1].tv_sec);
  EXPECT_EQ(info.st_mtim.tv_nsec, times[1].tv_nsec);
  times[0].tv_nsec = UTIME_OMIT;
  times[1].tv_nsec = UTIME_NOW;
  ASSERT_EQ(LIBC_NAMESPACE::futimens(fd, times), 0);
  ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &info), 0);
  EXPECT_EQ(info.st_atim.tv_sec, times[0].tv_sec);
  times[0].tv_nsec = 1000000000;
  EXPECT_EQ(LIBC_NAMESPACE::futimens(fd, times), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  ASSERT_EQ(LIBC_NAMESPACE::futimens(fd, nullptr), 0);
  ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
  EXPECT_EQ(LIBC_NAMESPACE::futimens(-1, nullptr), -1);
  EXPECT_EQ(int(libc_errno), EBADF);
}
