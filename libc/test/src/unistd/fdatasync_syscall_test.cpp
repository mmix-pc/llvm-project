//===-- fdatasync syscall injection tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
int result, calls, seen_fd;
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T> T fdatasync_test_syscall(long number, int fd) {
  EXPECT_EQ(number, long(SYS_fdatasync));
  ++calls;
  seen_fd = fd;
  return static_cast<T>(result);
}
} // namespace LIBC_NAMESPACE_DECL

// This exercises the raw provider, not target thread-cancellation transport.
#define syscall_impl fdatasync_test_syscall
#define fdatasync injected_fdatasync
#include "src/unistd/linux/fdatasync.cpp"
#undef fdatasync
#undef syscall_impl

TEST(LlvmLibcFdatasyncSyscallTest,
     ForwardsDescriptorAndPreservesErrnoOnSuccess) {
  result = 0;
  calls = 0;
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::injected_fdatasync(37), 0);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(seen_fd, 37);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}

TEST(LlvmLibcFdatasyncSyscallTest, PropagatesErrorsWithoutRetry) {
  const int errors[] = {EBADF, EINVAL, EINTR, EIO, ENOSPC, EDQUOT, ENOSYS};
  for (int error : errors) {
    result = -error;
    calls = 0;
    LIBC_NAMESPACE::libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::injected_fdatasync(-1), -1);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen_fd, -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
  }
}
