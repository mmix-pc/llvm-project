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
#include <linux/bfs_fs.h>
#include <linux/limits.h>
#include <linux/magic.h>

using LlvmLibcPathconfUtilsTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcPathconfUtilsTest, KnownFilesystemProperties) {
  struct statfs s = {};
  const struct {
    unsigned long type;
    long links;
  } cases[] = {{MINIX_SUPER_MAGIC, 250},    {MINIX_SUPER_MAGIC2, 250},
               {MINIX2_SUPER_MAGIC, 65530}, {MINIX2_SUPER_MAGIC2, 65530},
               {MINIX3_SUPER_MAGIC, 65530}, {XFS_SUPER_MAGIC, 0x7fffffff}};
  for (const auto &test : cases) {
    s.f_type = test.type;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_LINK_MAX), test.links);
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_2_SYMLINKS), 1L);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  constexpr unsigned long large_filesystems[] = {
      RAMFS_MAGIC, TMPFS_MAGIC, XFS_SUPER_MAGIC, BTRFS_SUPER_MAGIC};
  for (unsigned long type : large_filesystems) {
    s.f_type = type;
    libc_errno = EDOM;
#if __SIZEOF_LONG__ == 8
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_FILESIZEBITS), 64L);
    EXPECT_EQ(int(libc_errno), EDOM);
#else
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_FILESIZEBITS), -1L);
    EXPECT_EQ(int(libc_errno), ENOTSUP);
#endif
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_2_SYMLINKS), 1L);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  s.f_type = EXT4_SUPER_MAGIC;
  EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_2_SYMLINKS), 1L);
  constexpr unsigned long no_symlinks[] = {MSDOS_SUPER_MAGIC, BFS_MAGIC};
  for (unsigned long type : no_symlinks) {
    s.f_type = type;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_2_SYMLINKS), 0L);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  libc_errno = 0;
}

TEST_F(LlvmLibcPathconfUtilsTest, UnknownPropertiesAreNotGuessed) {
  struct statfs s = {};
  constexpr unsigned long unknown_types[] = {
      0, FUSE_SUPER_MAGIC, OVERLAYFS_SUPER_MAGIC, REISERFS_SUPER_MAGIC};
  constexpr int queries[] = {_PC_LINK_MAX, _PC_FILESIZEBITS, _PC_2_SYMLINKS};
  for (unsigned long type : unknown_types) {
    s.f_type = type;
    for (int name : queries) {
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, name), -1L);
      EXPECT_EQ(int(libc_errno), ENOTSUP);
    }
  }
  // A shared magic or a zero kernel s_max_links is insufficient metadata.
  constexpr unsigned long unknown_links[] = {EXT4_SUPER_MAGIC, TMPFS_MAGIC,
                                             RAMFS_MAGIC, BTRFS_SUPER_MAGIC};
  for (unsigned long type : unknown_links) {
    s.f_type = type;
    libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_LINK_MAX), -1L);
    EXPECT_EQ(int(libc_errno), ENOTSUP);
  }
  constexpr unsigned long unknown_sizes[] = {
      EXT4_SUPER_MAGIC, MINIX_SUPER_MAGIC, MSDOS_SUPER_MAGIC};
  for (unsigned long type : unknown_sizes) {
    s.f_type = type;
    libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_FILESIZEBITS), -1L);
    EXPECT_EQ(int(libc_errno), ENOTSUP);
  }
  libc_errno = 0;
}

TEST_F(LlvmLibcPathconfUtilsTest, LinuxLimitsPreserveErrno) {
  struct statfs s = {};
  const struct {
    int name;
    long value;
  } cases[] = {{_PC_PATH_MAX, PATH_MAX},
               {_PC_PIPE_BUF, PIPE_BUF},
               {_PC_MAX_CANON, MAX_CANON},
               {_PC_MAX_INPUT, MAX_INPUT}};
  for (const auto &test : cases) {
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, test.name), test.value);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  libc_errno = 0;
}

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

TEST_F(LlvmLibcPathconfUtilsTest, UnavailableProviders) {
#if defined(_PC_FALLOC) || defined(_PC_TEXTDOMAIN_MAX)
  struct statfs s = {};
#endif
#ifdef _PC_FALLOC
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_FALLOC), -1L);
  EXPECT_EQ(int(libc_errno), EDOM);
#endif
#ifdef _PC_TEXTDOMAIN_MAX
  libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::pathconfig(s, _PC_TEXTDOMAIN_MAX), -1L);
  EXPECT_EQ(int(libc_errno), ENOTSUP);
#endif
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
