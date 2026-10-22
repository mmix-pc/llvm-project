//===-- Per-object allocation query tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/linux/fallocate_query.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fstatfs.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <linux/magic.h>
#include <linux/stat.h>

namespace {
unsigned mode, mask, opens, stats, filesystems, closes;
unsigned long magic;
int open_error, stat_error, fs_error, close_error;
constexpr int FD = 73;

void reset() {
  mode = S_IFREG | 0600;
  mask = STATX_TYPE;
  magic = TMPFS_MAGIC;
  opens = stats = filesystems = closes = 0;
  open_error = stat_error = fs_error = close_error = 0;
  LIBC_NAMESPACE::libc_errno = EDOM;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace query_test_syscalls {
ErrorOr<int> open(const char *path, int flags, mode_t mode) {
  ++opens;
  EXPECT_STREQ(path, "file");
  EXPECT_EQ(flags, O_PATH | O_CLOEXEC);
  EXPECT_EQ(mode, mode_t(0));
  if (open_error)
    return Error(open_error);
  return FD;
}
ErrorOr<int> close(int fd) {
  ++closes;
  EXPECT_EQ(fd, FD);
  if (close_error)
    return Error(close_error);
  return 0;
}
ErrorOr<int> statx(int fd, const char *path, int flags, unsigned requested,
                  void *buffer) {
  ++stats;
  EXPECT_EQ(fd, FD);
  EXPECT_STREQ(path, "");
  EXPECT_EQ(flags, AT_EMPTY_PATH);
  EXPECT_EQ(requested, unsigned(STATX_TYPE));
  if (stat_error)
    return Error(stat_error);
  auto *object = static_cast<internal::kernel_statx_buf *>(buffer);
  object->stx_mask = mask;
  object->stx_mode = static_cast<uint16_t>(mode);
  return 0;
}
ErrorOr<int> fstatfs(int fd, struct statfs *buffer) {
  ++filesystems;
  EXPECT_EQ(fd, FD);
  if (fs_error)
    return Error(fs_error);
  buffer->f_type = magic;
  return 0;
}
} // namespace query_test_syscalls
} // namespace LIBC_NAMESPACE_DECL

#define linux_syscalls query_test_syscalls
#include "src/unistd/linux/fallocate_query.cpp"
#undef linux_syscalls

TEST(LlvmLibcFallocateQuery, KnownRegularFileRules) {
  const unsigned long types[] = {TMPFS_MAGIC, RAMFS_MAGIC};
  for (unsigned long type : types) {
    reset();
    magic = type;
    auto value = LIBC_NAMESPACE::fallocate_support(FD);
    ASSERT_TRUE(value.has_value());
#ifdef LIBC_HAS_POSIX_FALLOCATE
    EXPECT_EQ(*value, type == TMPFS_MAGIC ? 1L : -1L);
    EXPECT_EQ(filesystems, 1U);
#else
    EXPECT_EQ(*value, -1L);
    EXPECT_EQ(filesystems, 0U);
#endif
    EXPECT_EQ(stats, 1U);
    EXPECT_EQ(opens + closes, 0U);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  }
}

TEST(LlvmLibcFallocateQuery, NonRegularFilesAndMissingType) {
  const unsigned types[] = {S_IFDIR, S_IFIFO, S_IFBLK, S_IFCHR, S_IFSOCK, S_IFLNK};
  for (unsigned type : types) {
    reset();
    mode = type;
    auto value = LIBC_NAMESPACE::fallocate_support(FD);
    ASSERT_FALSE(value.has_value());
    EXPECT_EQ(value.error(), EINVAL);
    EXPECT_EQ(filesystems, 0U);
  }
  reset();
  mask = 0;
  auto value = LIBC_NAMESPACE::fallocate_support(FD);
  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), ENOTSUP);
}

TEST(LlvmLibcFallocateQuery, UnknownIsNotUnsupported) {
  const unsigned long types[] = {0, EXT4_SUPER_MAGIC, FUSE_SUPER_MAGIC,
                                 OVERLAYFS_SUPER_MAGIC};
  for (unsigned long type : types) {
    reset();
    magic = type;
    auto value = LIBC_NAMESPACE::fallocate_support(FD);
#ifdef LIBC_HAS_POSIX_FALLOCATE
    ASSERT_FALSE(value.has_value());
    EXPECT_EQ(value.error(), ENOTSUP);
#else
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, -1L);
#endif
  }
}

TEST(LlvmLibcFallocateQuery, PathPinsAndReleasesOneObject) {
  reset();
  auto value = LIBC_NAMESPACE::fallocate_support("file");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(opens, 1U);
  EXPECT_EQ(stats, 1U);
  EXPECT_EQ(closes, 1U);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}

TEST(LlvmLibcFallocateQuery, FailedQueriesReleaseWithoutRetry) {
  reset();
  open_error = EACCES;
  auto value = LIBC_NAMESPACE::fallocate_support("file");
  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), EACCES);
  EXPECT_EQ(stats + closes, 0U);

  reset();
  stat_error = EBADF;
  close_error = EINTR;
  value = LIBC_NAMESPACE::fallocate_support("file");
  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), EBADF);
  EXPECT_EQ(closes, 1U);

  reset();
  close_error = EINTR;
  value = LIBC_NAMESPACE::fallocate_support("file");
  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), EINTR);
  EXPECT_EQ(closes, 1U);
#ifdef LIBC_HAS_POSIX_FALLOCATE
  reset();
  fs_error = EIO;
  value = LIBC_NAMESPACE::fallocate_support("file");
  ASSERT_FALSE(value.has_value());
  EXPECT_EQ(value.error(), EIO);
  EXPECT_EQ(closes, 1U);
#endif
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}
