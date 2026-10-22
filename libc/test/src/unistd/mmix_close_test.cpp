//===-- Tests for MMIX close completion and descriptor consumption ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mmix_close_test_syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/unistd/close.h"
#include "src/unistd/posix_close.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
int reply, descriptor;
unsigned calls;
long number;
}

namespace LIBC_NAMESPACE_DECL {
int mmix_close_test_syscall(long nr, int fd) {
  ++calls;
  number = nr;
  descriptor = fd;
  return reply;
}

#ifdef TEST_CANCELLABLE_CLOSE
namespace mmix {
long cancellation_syscall(unsigned long nr, long fd, long, long, long, long, long) {
  return mmix_close_test_syscall(nr, static_cast<int>(fd));
}
} // namespace mmix
#endif
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcMmixClose, RawResultTranslationNeverRetries) {
  const int errors[] = {0, EINTR, EBADF, EIO, ENOSPC};
  for (int error : errors) {
    reply = -error;
    calls = 0;
    libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::close(17), error ? -1 : 0);
    EXPECT_EQ(static_cast<int>(libc_errno), error == EINTR ? EINPROGRESS : error);
    EXPECT_EQ(calls, 1U);
    EXPECT_EQ(descriptor, 17);
    EXPECT_EQ(number, long(SYS_close));
  }
}

TEST(LlvmLibcMmixClose, InvalidFlagsStillCloseExactlyOnce) {
  const int flags[] = {0, 1, -1};
  const int errors[] = {0, EINTR, EBADF, EIO};
  for (int flag : flags) {
    for (int error : errors) {
      calls = 0;
      reply = -error;
      libc_errno = 0;
      EXPECT_EQ(LIBC_NAMESPACE::posix_close(23, flag), (error || flag) ? -1 : 0);
      int expected = error == EINTR ? EINPROGRESS : error;
      if (!error && flag)
        expected = EINVAL;
      EXPECT_EQ(static_cast<int>(libc_errno), expected);
      EXPECT_EQ(calls, 1U);
      EXPECT_EQ(descriptor, 23);
    }
  }
}
