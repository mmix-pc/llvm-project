//===-- Type-only statx tests for realpath and directories
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <linux/stat.h>
#include <sys/syscall.h>

namespace {
using KernelStat = LIBC_NAMESPACE::internal::kernel_statx_buf;
KernelStat reply;
int calls, syscall_error;

void reset(unsigned mask = STATX_TYPE, unsigned mode = S_IFREG) {
  reply = {};
  reply.stx_mask = mask;
  reply.stx_mode = static_cast<uint16_t>(mode);
  calls = syscall_error = 0;
  LIBC_NAMESPACE::libc_errno = EDOM;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T type_test_syscall(long number, int, const char *, int, unsigned mask,
                    void *output) {
  EXPECT_EQ(number, long(SYS_statx));
  // Do not derive the oracle from the implementation's constant.
  EXPECT_EQ(mask, unsigned(STATX_TYPE));
  ++calls;
  if (syscall_error)
    return static_cast<T>(-syscall_error);
  *static_cast<KernelStat *>(output) = reply;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL

// Replace only statx transport; opening/checking/closing a directory remains
// real.
#define syscall_impl type_test_syscall
#define statx type_test_statx
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#undef syscall_impl

#define realpath type_test_realpath
#include "src/stdlib/linux/realpath.cpp"
#undef realpath

#define platform_check_dir type_test_check_dir
#define platform_dir_reclen type_test_dir_reclen
#define platform_dir_offset type_test_dir_offset
#define platform_seekdir type_test_seekdir
#include "src/__support/File/linux/dir.cpp"
#undef platform_seekdir
#undef platform_dir_offset
#undef platform_dir_reclen
#undef platform_check_dir
#undef statx

TEST(LlvmLibcRealpathStatxTest, MasksMatchUAPI) {
  EXPECT_EQ(LIBC_NAMESPACE::internal::KERNEL_STATX_TYPE_MASK,
            unsigned(STATX_TYPE));
  EXPECT_EQ(LIBC_NAMESPACE::internal::KERNEL_STATX_BASIC_STATS_MASK,
            unsigned(STATX_BASIC_STATS));
}

TEST(LlvmLibcRealpathStatxTest, TypeOnlyReply) {
  reset();
  char output[PATH_MAX] = "unchanged";
  EXPECT_EQ(LIBC_NAMESPACE::type_test_realpath("/entry", output), output);
  EXPECT_STREQ(output, "/entry");
  EXPECT_EQ(calls, 1);
}

TEST(LlvmLibcRealpathStatxTest, MissingTypeIsNotAcceptedAsMetadata) {
  reset(STATX_UID);
  char output[PATH_MAX] = "unchanged";
  EXPECT_EQ(LIBC_NAMESPACE::type_test_realpath("/entry", output), nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EIO);
  EXPECT_STREQ(output, "unchanged");
  EXPECT_EQ(calls, 1);
}

TEST(LlvmLibcRealpathStatxTest, SyscallError) {
  reset();
  syscall_error = EACCES;
  char output[PATH_MAX] = "unchanged";
  EXPECT_EQ(LIBC_NAMESPACE::type_test_realpath("/entry", output), nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EACCES);
  EXPECT_STREQ(output, "unchanged");
  EXPECT_EQ(calls, 1);
}

TEST(LlvmLibcRealpathStatxTest, DirectoryTypeOnlyReply) {
  const unsigned types[] = {S_IFDIR, S_IFREG};
  for (unsigned type : types) {
    reset(STATX_TYPE, type);
    int fd = LIBC_NAMESPACE::syscall_impl<int>(
        SYS_openat, AT_FDCWD, "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    ASSERT_GE(fd, 0);
    int result = LIBC_NAMESPACE::type_test_check_dir(fd);
    EXPECT_EQ(LIBC_NAMESPACE::syscall_impl<int>(SYS_close, fd), 0);
    EXPECT_EQ(result, type == S_IFDIR ? 0 : ENOTDIR);
    EXPECT_EQ(calls, 1);
  }
}
