//===-- Unittests for fchmodat --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/types/struct_stat.h"
#include "src/fcntl/open.h"
#include "src/sys/stat/fchmodat.h"
#include "src/sys/stat/fstat.h"
#include "src/unistd/close.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::testing::ErrnoSetterMatcher;
using LlvmLibcFchmodatTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcFchmodatTest, ChangeAndOpen) {
  // The test file is initially writable. We open it for writing and ensure
  // that it indeed can be opened for writing. Next, we close the file and
  // make it readonly using chmod. We test that chmod actually succeeded by
  // trying to open the file for writing and failing.
  constexpr const char *TEST_FILE = "testdata/fchmodat.test";
  constexpr const char *TEST_DIR = "testdata";
  constexpr const char *TEST_FILE_BASENAME = "fchmodat.test";
  const char WRITE_DATA[] = "fchmodat test";
  constexpr ssize_t WRITE_SIZE = ssize_t(sizeof(WRITE_DATA));

  int fd = LIBC_NAMESPACE::open(TEST_FILE, O_CREAT | O_WRONLY, S_IRWXU);
  ASSERT_GT(fd, 0);
  ASSERT_ERRNO_SUCCESS();
  ASSERT_EQ(LIBC_NAMESPACE::write(fd, WRITE_DATA, sizeof(WRITE_DATA)),
            WRITE_SIZE);
  ASSERT_THAT(LIBC_NAMESPACE::close(fd), Succeeds(0));

  int dirfd = LIBC_NAMESPACE::open(TEST_DIR, O_DIRECTORY);
  ASSERT_GT(dirfd, 0);
  ASSERT_ERRNO_SUCCESS();

  EXPECT_THAT(LIBC_NAMESPACE::fchmodat(dirfd, TEST_FILE_BASENAME, S_IRUSR, 0),
              Succeeds(0));

  // Opening for writing should fail.
  EXPECT_EQ(LIBC_NAMESPACE::open(TEST_FILE, O_APPEND | O_WRONLY), -1);
  ASSERT_ERRNO_FAILURE();
  // But opening for reading should succeed.
  fd = LIBC_NAMESPACE::open(TEST_FILE, O_APPEND | O_RDONLY);
  EXPECT_GT(fd, 0);
  ASSERT_ERRNO_SUCCESS();

  EXPECT_THAT(LIBC_NAMESPACE::close(fd), Succeeds(0));
  EXPECT_THAT(LIBC_NAMESPACE::fchmodat(dirfd, TEST_FILE_BASENAME, S_IRWXU, 0),
              Succeeds(0));

  EXPECT_THAT(LIBC_NAMESPACE::close(dirfd), Succeeds(0));
}

TEST_F(LlvmLibcFchmodatTest, NonExistentFile) {
  ASSERT_THAT(
      LIBC_NAMESPACE::fchmodat(AT_FDCWD, "non-existent-file", S_IRUSR, 0),
      Fails(ENOENT));
}

class LlvmLibcFchmodatFlagsTest
    : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
protected:
  static constexpr const char *TARGET = "testdata/fchmodat-flags.test";
  static constexpr const char *LINK = "testdata/fchmodat-flags-link.test";
  int fd = -1;
  bool has_link = false;

  void TearDown() override {
    if (has_link)
      EXPECT_THAT(LIBC_NAMESPACE::unlink(LINK), Succeeds(0));
    if (fd >= 0) {
      EXPECT_THAT(LIBC_NAMESPACE::close(fd), Succeeds(0));
      EXPECT_THAT(LIBC_NAMESPACE::unlink(TARGET), Succeeds(0));
    }
    ErrnoCheckingTest::TearDown();
  }
};

TEST_F(LlvmLibcFchmodatFlagsTest, InvalidFlagsDoNotChangeMode) {
  fd = LIBC_NAMESPACE::open(TARGET, O_CREAT | O_EXCL | O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  ASSERT_THAT(LIBC_NAMESPACE::fchmodat(AT_FDCWD, TARGET, 0600, 0), Succeeds(0));
  const int invalid_flags[] = {AT_SYMLINK_FOLLOW, AT_REMOVEDIR, -1,
                               AT_SYMLINK_NOFOLLOW | AT_SYMLINK_FOLLOW};
  for (int flags : invalid_flags) {
    EXPECT_THAT(LIBC_NAMESPACE::fchmodat(AT_FDCWD, TARGET, 0400, flags),
                Fails(EINVAL));
    struct stat st;
    ASSERT_THAT(LIBC_NAMESPACE::fstat(fd, &st), Succeeds(0));
    EXPECT_EQ(st.st_mode & mode_t(0777), mode_t(0600));
  }
}

TEST_F(LlvmLibcFchmodatFlagsTest, NoFollowNeverChangesSymlinkTarget) {
  fd = LIBC_NAMESPACE::open(TARGET, O_CREAT | O_EXCL | O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  ASSERT_THAT(LIBC_NAMESPACE::fchmodat(AT_FDCWD, TARGET, 0600, 0), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::symlink("fchmodat-flags.test", LINK),
              Succeeds(0));
  has_link = true;

  // A filesystem may support changing the symlink's mode, or reject it. In
  // either case the target must stay unchanged, including on older kernels.
  int result =
      LIBC_NAMESPACE::fchmodat(AT_FDCWD, LINK, 0400, AT_SYMLINK_NOFOLLOW);
  if (result == -1) {
    ASSERT_ERRNO_EQ(EOPNOTSUPP);
  } else {
    EXPECT_EQ(result, 0);
    ASSERT_ERRNO_SUCCESS();
  }
  struct stat st;
  ASSERT_THAT(LIBC_NAMESPACE::fstat(fd, &st), Succeeds(0));
  EXPECT_EQ(st.st_mode & mode_t(0777), mode_t(0600));

  // The same path must still follow the symlink when flags are zero.
  EXPECT_THAT(LIBC_NAMESPACE::fchmodat(AT_FDCWD, LINK, 0400, 0), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::fstat(fd, &st), Succeeds(0));
  EXPECT_EQ(st.st_mode & mode_t(0777), mode_t(0400));
}

TEST_F(LlvmLibcFchmodatFlagsTest, NoFollowOnRegularFile) {
  fd = LIBC_NAMESPACE::open(TARGET, O_CREAT | O_EXCL | O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  ASSERT_THAT(LIBC_NAMESPACE::fchmodat(AT_FDCWD, TARGET, 0600, 0), Succeeds(0));
  int result =
      LIBC_NAMESPACE::fchmodat(AT_FDCWD, TARGET, 0400, AT_SYMLINK_NOFOLLOW);
  if (result == -1) {
    ASSERT_ERRNO_EQ(EOPNOTSUPP);
  } else {
    EXPECT_EQ(result, 0);
    ASSERT_ERRNO_SUCCESS();
  }
  struct stat st;
  ASSERT_THAT(LIBC_NAMESPACE::fstat(fd, &st), Succeeds(0));
  EXPECT_EQ(st.st_mode & mode_t(0777), mode_t(result == 0 ? 0400 : 0600));
}
