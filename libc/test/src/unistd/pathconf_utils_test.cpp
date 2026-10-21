//===-- Unittests for Linux pathconf utilities ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/limits_macros.h"
#include "hdr/unistd_macros.h"
#include "src/unistd/linux/pathconf_utils.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include <linux/magic.h>

using LlvmLibcPathconfUtilsTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcPathconfUtilsTest, ResolutionConversion) {
  constexpr unsigned long long values[] = {
      1, 1000000000, static_cast<unsigned long long>(LONG_MAX)};
  for (unsigned long long value : values) {
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::checked_timestamp_resolution(value),
              static_cast<long>(value));
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  EXPECT_EQ(LIBC_NAMESPACE::checked_timestamp_resolution(
                static_cast<unsigned long long>(LONG_MAX) + 1),
            -1L);
  EXPECT_EQ(int(libc_errno), EOVERFLOW);
  libc_errno = 0;
}

#ifdef _PC_TIMESTAMP_RESOLUTION
TEST_F(LlvmLibcPathconfUtilsTest, KnownFilesystemResolutions) {
  struct statfs s = {};
  constexpr unsigned long nanosecond_types[] = {
      RAMFS_MAGIC, TMPFS_MAGIC, BTRFS_SUPER_MAGIC, XFS_SUPER_MAGIC};
  for (unsigned long type : nanosecond_types) {
    s.f_type = type;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_TIMESTAMP_RESOLUTION), 1L);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  constexpr int second_types[] = {MINIX_SUPER_MAGIC, MINIX_SUPER_MAGIC2,
                                  MINIX2_SUPER_MAGIC, MINIX2_SUPER_MAGIC2,
                                  MINIX3_SUPER_MAGIC};
  for (int type : second_types) {
    s.f_type = type;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_TIMESTAMP_RESOLUTION),
              1000000000L);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  libc_errno = 0;
}

TEST_F(LlvmLibcPathconfUtilsTest, MissingMetadataIsNotAnIndeterminateLimit) {
  struct statfs s = {};
  constexpr int unknown_types[] = {0, EXT4_SUPER_MAGIC, FUSE_SUPER_MAGIC,
                                   OVERLAYFS_SUPER_MAGIC, MSDOS_SUPER_MAGIC};
  for (int type : unknown_types) {
    s.f_type = type;
    libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_TIMESTAMP_RESOLUTION), -1L);
    EXPECT_EQ(int(libc_errno), ENOTSUP);
  }
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_SOCK_MAXBUF), -1L);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, -1), -1L);
  EXPECT_EQ(int(libc_errno), EINVAL);
  libc_errno = 0;
}
#endif
