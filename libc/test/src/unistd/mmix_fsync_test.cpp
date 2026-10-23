//===-- MMIX fsync cancellation transport tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
long reply, seen_fd;
unsigned calls;
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
long cancellation_syscall(unsigned long number, long fd, long a2, long a3,
                          long a4, long a5, long a6) {
  EXPECT_EQ(number, static_cast<unsigned long>(SYS_fsync));
  EXPECT_EQ(a2 | a3 | a4 | a5 | a6, 0L);
  ++calls;
  seen_fd = fd;
  return reply;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#include "src/unistd/linux/mmix/cancellation_fsync.cpp"

TEST(LlvmLibcMmixFsync, PreservesDescriptorErrorsAndErrno) {
  const int errors[] = {0, EBADF, EINVAL, EINTR, EIO, ENOSPC, EDQUOT};
  const int descriptors[] = {-1, 37};
  for (int fd : descriptors) {
    for (int error : errors) {
      calls = 0;
      reply = -error;
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::fsync(fd), error ? -1 : 0);
      EXPECT_EQ(calls, 1U);
      EXPECT_EQ(seen_fd, long(fd));
      EXPECT_EQ(int(libc_errno), error ? error : EDOM);
    }
  }
}
