//===-- Unittests for fstatat ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/fcntl/open.h"
#include "src/sys/stat/fstat.h"
#include "src/sys/stat/fstatat.h"
#include "src/sys/stat/utimensat.h"
#include "src/unistd/close.h"
#include "src/unistd/getcwd.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::testing::ErrnoSetterMatcher;
using LlvmLibcFstatatTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcFstatatTest, RelativeAndEmptyPath) {
  constexpr const char *PATH = "testdata/fstatat.test";
  int fd = LIBC_NAMESPACE::open(PATH, O_CREAT | O_TRUNC | O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(LIBC_NAMESPACE::write(fd, "abc", 3), ssize_t(3));
  const struct timespec times[] = {{2147483650LL, 123456789},
                                   {2147483651LL, 987654321}};
  ASSERT_THAT(LIBC_NAMESPACE::utimensat(AT_FDCWD, PATH, times, 0), Succeeds(0));
  struct stat expected, actual;
  ASSERT_THAT(LIBC_NAMESPACE::fstat(fd, &expected), Succeeds(0));
  ASSERT_EQ(expected.st_mtim.tv_sec, time_t(2147483651LL));
  int directory = LIBC_NAMESPACE::open("testdata", O_RDONLY | O_DIRECTORY);
  ASSERT_GE(directory, 0);
  ASSERT_THAT(LIBC_NAMESPACE::fstatat(directory, "fstatat.test", &actual,
                                      AT_NO_AUTOMOUNT),
              Succeeds(0));
  EXPECT_EQ(actual.st_ino, expected.st_ino);
  EXPECT_EQ(actual.st_dev, expected.st_dev);
  EXPECT_EQ(actual.st_mode, expected.st_mode);
  EXPECT_EQ(actual.st_size, off_t(3));
  EXPECT_EQ(actual.st_atim.tv_sec, expected.st_atim.tv_sec);
  EXPECT_EQ(actual.st_atim.tv_nsec, expected.st_atim.tv_nsec);
  EXPECT_EQ(actual.st_mtim.tv_sec, expected.st_mtim.tv_sec);
  EXPECT_EQ(actual.st_mtim.tv_nsec, expected.st_mtim.tv_nsec);
  ASSERT_THAT(LIBC_NAMESPACE::fstatat(fd, "", &actual, AT_EMPTY_PATH),
              Succeeds(0));
  EXPECT_EQ(actual.st_ino, expected.st_ino);
  EXPECT_EQ(actual.st_size, expected.st_size);
  EXPECT_THAT(LIBC_NAMESPACE::fstatat(fd, "child", &actual, 0), Fails(ENOTDIR));
  ASSERT_THAT(LIBC_NAMESPACE::close(directory), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::close(fd), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::unlink(PATH), Succeeds(0));
}

TEST_F(LlvmLibcFstatatTest, Symlink) {
  constexpr const char *PATH = "testdata/fstatat-target.test";
  constexpr const char *LINK = "testdata/fstatat-link.test";
  int fd = LIBC_NAMESPACE::open(PATH, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  ASSERT_GE(fd, 0);
  ASSERT_THAT(LIBC_NAMESPACE::close(fd), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::symlink("fstatat-target.test", LINK),
              Succeeds(0));
  struct stat actual;
  ASSERT_THAT(LIBC_NAMESPACE::fstatat(AT_FDCWD, LINK, &actual, 0), Succeeds(0));
  EXPECT_TRUE(S_ISREG(actual.st_mode));
  ASSERT_THAT(
      LIBC_NAMESPACE::fstatat(AT_FDCWD, LINK, &actual, AT_SYMLINK_NOFOLLOW),
      Succeeds(0));
  EXPECT_TRUE(S_ISLNK(actual.st_mode));
  ASSERT_THAT(LIBC_NAMESPACE::unlink(PATH), Succeeds(0));
  EXPECT_THAT(LIBC_NAMESPACE::fstatat(AT_FDCWD, LINK, &actual, 0),
              Fails(ENOENT));
  EXPECT_THAT(
      LIBC_NAMESPACE::fstatat(AT_FDCWD, LINK, &actual, AT_SYMLINK_NOFOLLOW),
      Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::unlink(LINK), Succeeds(0));
}

TEST_F(LlvmLibcFstatatTest, AbsoluteAndErrors) {
  char path[4096];
  ASSERT_NE(LIBC_NAMESPACE::getcwd(path, sizeof(path)), nullptr);
  struct stat actual;
  ASSERT_THAT(LIBC_NAMESPACE::fstatat(-1, path, &actual, 0), Succeeds(0));
  EXPECT_TRUE(S_ISDIR(actual.st_mode));
  EXPECT_THAT(LIBC_NAMESPACE::fstatat(-1, "relative", &actual, 0),
              Fails(EBADF));
  EXPECT_THAT(LIBC_NAMESPACE::fstatat(AT_FDCWD, "", &actual, 0), Fails(ENOENT));
  // These statx synchronization flags must not leak into fstatat.
  constexpr int invalid_flags[] = {0x2000, 0x4000, AT_REMOVEDIR, -1};
  for (int flags : invalid_flags)
    EXPECT_THAT(LIBC_NAMESPACE::fstatat(AT_FDCWD, ".", &actual, flags),
                Fails(EINVAL));
}
