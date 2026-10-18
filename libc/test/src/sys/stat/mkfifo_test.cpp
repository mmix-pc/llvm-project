//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/fcntl/open.h"
#include "src/sys/stat/fstatat.h"
#include "src/sys/stat/mkfifo.h"
#include "src/sys/stat/mkfifoat.h"
#include "src/sys/stat/umask.h"
#include "src/unistd/close.h"
#include "src/unistd/getcwd.h"
#include "src/unistd/unlink.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::testing::ErrnoSetterMatcher;
using LlvmLibcMkfifoTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcMkfifoTest, TypePermissionsAndUmask) {
  constexpr const char *path = "testdata/mkfifo.fifo";
  mode_t previous = LIBC_NAMESPACE::umask(0027);
  int result = LIBC_NAMESPACE::mkfifo(path, S_IFREG | 0766);
  LIBC_NAMESPACE::umask(previous);
  ASSERT_THAT(result, Succeeds(0));
  struct stat st;
  ASSERT_THAT(LIBC_NAMESPACE::fstatat(AT_FDCWD, path, &st, 0), Succeeds(0));
  EXPECT_TRUE(S_ISFIFO(st.st_mode));
  EXPECT_EQ(st.st_mode & 0777, mode_t(0740));
  EXPECT_THAT(LIBC_NAMESPACE::mkfifo(path, 0600), Fails(EEXIST));
  ASSERT_THAT(LIBC_NAMESPACE::unlink(path), Succeeds(0));
}

TEST_F(LlvmLibcMkfifoTest, DirectoryRelative) {
  int dir = LIBC_NAMESPACE::open("testdata", O_RDONLY | O_DIRECTORY);
  ASSERT_GE(dir, 0);
  mode_t previous = LIBC_NAMESPACE::umask(0077);
  int result = LIBC_NAMESPACE::mkfifoat(dir, "mkfifoat.fifo", S_IFCHR | 0666);
  LIBC_NAMESPACE::umask(previous);
  ASSERT_THAT(result, Succeeds(0));
  struct stat st;
  ASSERT_THAT(LIBC_NAMESPACE::fstatat(dir, "mkfifoat.fifo", &st, 0), Succeeds(0));
  EXPECT_TRUE(S_ISFIFO(st.st_mode));
  EXPECT_EQ(st.st_mode & 0777, mode_t(0600));
  EXPECT_THAT(LIBC_NAMESPACE::mkfifoat(dir, "mkfifoat.fifo", 0600),
              Fails(EEXIST));
  ASSERT_THAT(LIBC_NAMESPACE::unlink("testdata/mkfifoat.fifo"), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::close(dir), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::mkfifoat(AT_FDCWD, "testdata/mkfifoat.fifo", 0600),
              Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::unlink("testdata/mkfifoat.fifo"), Succeeds(0));
}

TEST_F(LlvmLibcMkfifoTest, AbsolutePathAndErrors) {
  char path[4096];
  ASSERT_NE(LIBC_NAMESPACE::getcwd(path, sizeof(path)), nullptr);
  size_t length = 0;
  while (path[length])
    ++length;
  constexpr char suffix[] = "/testdata/mkfifoat.absolute";
  ASSERT_LT(length + sizeof(suffix), sizeof(path));
  for (size_t i = 0; i < sizeof(suffix); ++i)
    path[length + i] = suffix[i];
  ASSERT_THAT(LIBC_NAMESPACE::mkfifoat(-1, path, 0600), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::unlink(path), Succeeds(0));
  EXPECT_THAT(LIBC_NAMESPACE::mkfifo("", 0600), Fails(ENOENT));
  EXPECT_THAT(LIBC_NAMESPACE::mkfifo("testdata/missing-dir/fifo", 0600),
              Fails(ENOENT));
  EXPECT_THAT(LIBC_NAMESPACE::mkfifoat(AT_FDCWD, "", 0600), Fails(ENOENT));
  EXPECT_THAT(LIBC_NAMESPACE::mkfifoat(-1, "relative", 0600), Fails(EBADF));
  int fd = LIBC_NAMESPACE::open("testdata/mkfifo.fd", O_CREAT | O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  EXPECT_THAT(LIBC_NAMESPACE::mkfifoat(fd, "child", 0600), Fails(ENOTDIR));
  EXPECT_THAT(LIBC_NAMESPACE::mkfifo("testdata/mkfifo.fd/child", 0600),
              Fails(ENOTDIR));
  ASSERT_THAT(LIBC_NAMESPACE::close(fd), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::unlink("testdata/mkfifo.fd"), Succeeds(0));
}
