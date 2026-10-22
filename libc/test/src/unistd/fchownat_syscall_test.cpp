//===-- fchownat syscall injection tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/types/gid_t.h"
#include "hdr/types/uid_t.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
int result, calls, seen_fd, seen_flags;
const char *seen_path;
uid_t seen_owner;
gid_t seen_group;
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T fchownat_test_syscall(long number, int fd, const char *path, uid_t owner,
                        gid_t group, int flags) {
  EXPECT_EQ(number, long(SYS_fchownat));
  ++calls;
  seen_fd = fd;
  seen_path = path;
  seen_owner = owner;
  seen_group = group;
  seen_flags = flags;
  return static_cast<T>(result);
}
} // namespace LIBC_NAMESPACE_DECL

// Include the real provider without interposing on the test runner's syscalls.
#define syscall_impl fchownat_test_syscall
#define fchownat injected_fchownat
#include "src/unistd/linux/fchownat.cpp"
#undef fchownat
#undef syscall_impl

TEST(LlvmLibcFchownatSyscallTest, ForwardsArgumentsAndPreservesErrnoOnSuccess) {
  const char path[] = "relative/link";
  result = 0;
  calls = 0;
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::injected_fchownat(
                37, path, uid_t(-1), gid_t(0x80000001U), AT_SYMLINK_NOFOLLOW),
            0);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(seen_fd, 37);
  EXPECT_EQ(seen_path, path);
  EXPECT_EQ(seen_owner, uid_t(-1));
  EXPECT_EQ(seen_group, gid_t(0x80000001U));
  EXPECT_EQ(seen_flags, AT_SYMLINK_NOFOLLOW);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);

  EXPECT_EQ(LIBC_NAMESPACE::injected_fchownat(AT_FDCWD, path,
                                              uid_t(0x80000002U), gid_t(-1), 0),
            0);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(seen_fd, AT_FDCWD);
  EXPECT_EQ(seen_owner, uid_t(0x80000002U));
  EXPECT_EQ(seen_group, gid_t(-1));
  EXPECT_EQ(seen_flags, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}

TEST(LlvmLibcFchownatSyscallTest, PropagatesErrorsWithoutRetryOrFlagFiltering) {
  const int errors[] = {EBADF,  EACCES, EPERM, ENOENT,
                        EINVAL, EROFS,  EINTR, EIO};
  for (int error : errors) {
    result = -error;
    calls = 0;
    LIBC_NAMESPACE::libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::injected_fchownat(-1, nullptr, uid_t(-1),
                                                gid_t(-1), 0x40000000),
              -1);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen_fd, -1);
    EXPECT_EQ(seen_path, nullptr);
    EXPECT_EQ(seen_flags, 0x40000000);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
  }
}
