//===-- posix_fallocate syscall injection tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/types/off_t.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

#include <sys/syscall.h>

namespace {
long result;
int calls, seen_fd, seen_mode;
off_t seen_offset, seen_len;
constexpr off_t MAX_OFFSET = LIBC_NAMESPACE::cpp::numeric_limits<off_t>::max();
constexpr off_t MIN_OFFSET = LIBC_NAMESPACE::cpp::numeric_limits<off_t>::min();
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T fallocate_test_syscall(long number, int fd, int mode, off_t offset,
                         off_t len) {
  EXPECT_EQ(number, long(SYS_fallocate));
  ++calls;
  seen_fd = fd;
  seen_mode = mode;
  seen_offset = offset;
  seen_len = len;
  return static_cast<T>(result);
}
} // namespace LIBC_NAMESPACE_DECL

// Exercise the actual provider, without interposing on the runner's syscalls.
// This observes transport/error policy, not filesystem reservation correctness.
#define syscall_impl fallocate_test_syscall
#define posix_fallocate injected_posix_fallocate
#include "src/fcntl/linux/posix_fallocate.cpp"
#undef posix_fallocate
#undef syscall_impl

TEST(LlvmLibcPosixFallocateSyscallTest, ModeZeroAndFullWidthRanges) {
  const off_t ranges[][2] = {{0, 1},
                             {(off_t(1) << 40) + 17, (off_t(1) << 41) + 31},
                             {MAX_OFFSET - 1, 1},
                             {0, MAX_OFFSET}};
  for (const auto &range : ranges) {
    result = 0;
    calls = 0;
    LIBC_NAMESPACE::libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::injected_posix_fallocate(37, range[0], range[1]),
              0);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen_fd, 37);
    EXPECT_EQ(seen_mode, 0);
    EXPECT_EQ(seen_offset, range[0]);
    EXPECT_EQ(seen_len, range[1]);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  }
}

TEST(LlvmLibcPosixFallocateSyscallTest, RejectsInvalidRangesBeforeSyscall) {
  struct Case {
    off_t offset, len;
    int error;
  };
  const Case cases[] = {{-1, 1, EINVAL},
                        {MIN_OFFSET, 1, EINVAL},
                        {0, 0, EINVAL},
                        {0, -1, EINVAL},
                        {0, MIN_OFFSET, EINVAL},
                        {MAX_OFFSET, 1, EFBIG},
                        {MAX_OFFSET - 1, 2, EFBIG},
                        {1, MAX_OFFSET, EFBIG},
                        {MAX_OFFSET, MAX_OFFSET, EFBIG}};
  for (const auto &test : cases) {
    result = 0;
    calls = 0;
    LIBC_NAMESPACE::libc_errno = ERANGE;
    EXPECT_EQ(
        LIBC_NAMESPACE::injected_posix_fallocate(37, test.offset, test.len),
        test.error);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  }
}

TEST(LlvmLibcPosixFallocateSyscallTest, DirectErrorsWithoutRetryOrFallback) {
  const int errors[] = {EBADF,  EINVAL, EFBIG, EOPNOTSUPP, ENOSYS, ENOSPC,
                        EDQUOT, EINTR,  EIO,   EPERM,      EACCES, ESPIPE};
  const int saved_errnos[] = {0, EDOM};
  for (int error : errors) {
    for (int saved_errno : saved_errnos) {
      result = -error;
      calls = 0;
      LIBC_NAMESPACE::libc_errno = saved_errno;
      EXPECT_EQ(LIBC_NAMESPACE::injected_posix_fallocate(-1, 4096, 8192),
                error);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_fd, -1);
      EXPECT_EQ(seen_mode, 0);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
    }
  }
}
