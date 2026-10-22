//===-- Unittests for fchownat --------------------------------------------===//
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
#include "src/sys/stat/fstatat.h"
#include "src/unistd/close.h"
#include "src/unistd/fchownat.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlinkat.h"
#include "test/UnitTest/Test.h"

class LlvmLibcFchownatTest : public LIBC_NAMESPACE::testing::Test {
protected:
  int dir = -1, fd = -1;
  bool link_created = false;

  void TearDown() override {
    if (link_created)
      EXPECT_EQ(LIBC_NAMESPACE::unlinkat(dir, "fchownat.link", 0), 0);
    if (fd >= 0) {
      EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
      EXPECT_EQ(LIBC_NAMESPACE::unlinkat(dir, "fchownat.file", 0), 0);
    }
    if (dir >= 0)
      EXPECT_EQ(LIBC_NAMESPACE::close(dir), 0);
  }
};

TEST_F(LlvmLibcFchownatTest, RelativeOwnershipAndUnchangedMetadataOnFailure) {
  dir = LIBC_NAMESPACE::open(libc_make_test_file_path("testdata"),
                             O_RDONLY | O_DIRECTORY);
  ASSERT_GE(dir, 0);
  fd = LIBC_NAMESPACE::openat(dir, "fchownat.file", O_CREAT | O_EXCL | O_RDWR,
                              S_IRUSR | S_IWUSR);
  ASSERT_GE(fd, 0);
  struct stat before, after;
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(dir, "fchownat.file", &before, 0), 0);

  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::fchownat(dir, "fchownat.file", before.st_uid,
                                     before.st_gid, 0),
            0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::fchownat(
                AT_FDCWD, libc_make_test_file_path("testdata/fchownat.file"),
                uid_t(-1), gid_t(-1), 0),
            0);
  EXPECT_EQ(LIBC_NAMESPACE::fchownat(dir, "fchownat.file", uid_t(-1), gid_t(-1),
                                     0x40000000),
            -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(
      LIBC_NAMESPACE::fchownat(-1, "fchownat.file", uid_t(-1), gid_t(-1), 0),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(
      LIBC_NAMESPACE::fchownat(fd, "fchownat.file", uid_t(-1), gid_t(-1), 0),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOTDIR);
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(dir, "fchownat.file", &after, 0), 0);
  EXPECT_EQ(after.st_uid, before.st_uid);
  EXPECT_EQ(after.st_gid, before.st_gid);
  EXPECT_EQ(after.st_ino, before.st_ino);
}

TEST_F(LlvmLibcFchownatTest, NoFollowOperatesOnSymlinkLoop) {
  dir = LIBC_NAMESPACE::open(libc_make_test_file_path("testdata"),
                             O_RDONLY | O_DIRECTORY);
  ASSERT_GE(dir, 0);
  // Resolving a self-referential link must fail; no-follow must reach the link.
  int ret = LIBC_NAMESPACE::symlink(
      "fchownat.link", libc_make_test_file_path("testdata/fchownat.link"));
  link_created = ret == 0;
  ASSERT_EQ(ret, 0);
  struct stat before, after;
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(dir, "fchownat.link", &before,
                                    AT_SYMLINK_NOFOLLOW),
            0);
  EXPECT_EQ(LIBC_NAMESPACE::fchownat(dir, "fchownat.link", uid_t(-1), gid_t(-1),
                                     AT_SYMLINK_NOFOLLOW),
            0);
  EXPECT_EQ(
      LIBC_NAMESPACE::fchownat(dir, "fchownat.link", uid_t(-1), gid_t(-1), 0),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ELOOP);
  ASSERT_EQ(LIBC_NAMESPACE::fstatat(dir, "fchownat.link", &after,
                                    AT_SYMLINK_NOFOLLOW),
            0);
  EXPECT_TRUE(S_ISLNK(after.st_mode));
  EXPECT_EQ(after.st_ino, before.st_ino);
  EXPECT_EQ(after.st_uid, before.st_uid);
  EXPECT_EQ(after.st_gid, before.st_gid);
}
