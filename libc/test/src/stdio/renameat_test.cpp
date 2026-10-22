//===-- Unittests for renameat --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/fcntl/openat.h"
#include "src/stdio/renameat.h"
#include "src/sys/stat/fstat.h"
#include "src/sys/stat/fstatat.h"
#include "src/unistd/close.h"
#include "src/unistd/unlinkat.h"
#include "test/UnitTest/Test.h"

class LlvmLibcRenameatTest : public LIBC_NAMESPACE::testing::Test {
protected:
  int olddir = -1, newdir = -1, oldfd = -1, newfd = -1;
  bool old_exists = false, new_exists = false;

  void TearDown() override {
    if (oldfd >= 0)
      EXPECT_EQ(LIBC_NAMESPACE::close(oldfd), 0);
    if (newfd >= 0)
      EXPECT_EQ(LIBC_NAMESPACE::close(newfd), 0);
    if (old_exists)
      EXPECT_EQ(LIBC_NAMESPACE::unlinkat(olddir, "renameat.file", 0), 0);
    if (new_exists)
      EXPECT_EQ(LIBC_NAMESPACE::unlinkat(newdir, "renameat.file", 0), 0);
    if (olddir >= 0)
      EXPECT_EQ(LIBC_NAMESPACE::close(olddir), 0);
    if (newdir >= 0)
      EXPECT_EQ(LIBC_NAMESPACE::close(newdir), 0);
  }
};

TEST_F(LlvmLibcRenameatTest, ReplacementPreservesOpenReferences) {
  olddir = LIBC_NAMESPACE::open(libc_make_test_file_path("testdata"),
                                O_RDONLY | O_DIRECTORY);
  ASSERT_GE(olddir, 0);
  newdir = LIBC_NAMESPACE::open(libc_make_test_file_path("."),
                                O_RDONLY | O_DIRECTORY);
  ASSERT_GE(newdir, 0);
  oldfd = LIBC_NAMESPACE::openat(olddir, "renameat.file",
                                 O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
  old_exists = oldfd >= 0;
  ASSERT_GE(oldfd, 0);
  newfd = LIBC_NAMESPACE::openat(newdir, "renameat.file",
                                 O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
  new_exists = newfd >= 0;
  ASSERT_GE(newfd, 0);
  struct stat source, target, observed;
  ASSERT_EQ(LIBC_NAMESPACE::fstat(oldfd, &source), 0);
  ASSERT_EQ(LIBC_NAMESPACE::fstat(newfd, &target), 0);

  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::renameat(olddir, "renameat.file", olddir,
                                     "renameat.file"),
            0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(olddir, "renameat.file", &observed, 0), 0);
  EXPECT_EQ(observed.st_ino, source.st_ino);

  EXPECT_EQ(
      LIBC_NAMESPACE::renameat(-1, "renameat.file", newdir, "renameat.file"),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(
      LIBC_NAMESPACE::renameat(olddir, "renameat.file", -1, "renameat.file"),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(
      LIBC_NAMESPACE::renameat(oldfd, "renameat.file", newdir, "renameat.file"),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOTDIR);
  // An empty source cannot resolve, regardless of pre-existing test files.
  EXPECT_EQ(LIBC_NAMESPACE::renameat(olddir, "", newdir, "renameat.file"), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(olddir, "renameat.file", &observed, 0), 0);
  EXPECT_EQ(observed.st_ino, source.st_ino);
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(newdir, "renameat.file", &observed, 0), 0);
  EXPECT_EQ(observed.st_ino, target.st_ino);

  LIBC_NAMESPACE::libc_errno = EDOM;
  int ret = LIBC_NAMESPACE::renameat(olddir, "renameat.file", newdir,
                                     "renameat.file");
  if (ret == 0)
    old_exists = false;
  ASSERT_EQ(ret, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::fstatat(olddir, "renameat.file", &observed, 0), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(newdir, "renameat.file", &observed, 0), 0);
  EXPECT_EQ(observed.st_ino, source.st_ino);
  EXPECT_EQ(observed.st_dev, source.st_dev);
  ASSERT_EQ(LIBC_NAMESPACE::fstat(oldfd, &observed), 0);
  EXPECT_EQ(observed.st_ino, source.st_ino);
  EXPECT_EQ(observed.st_nlink, source.st_nlink);
  ASSERT_EQ(LIBC_NAMESPACE::fstat(newfd, &observed), 0);
  EXPECT_EQ(observed.st_ino, target.st_ino);
  EXPECT_EQ(observed.st_dev, target.st_dev);
  EXPECT_EQ(observed.st_nlink, decltype(observed.st_nlink)(0));

  ret = LIBC_NAMESPACE::renameat(AT_FDCWD,
                                 libc_make_test_file_path("renameat.file"),
                                 olddir, "renameat.file");
  if (ret == 0) {
    old_exists = true;
    new_exists = false;
  }
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(olddir, "renameat.file", &observed, 0), 0);
  EXPECT_EQ(observed.st_ino, source.st_ino);
}
