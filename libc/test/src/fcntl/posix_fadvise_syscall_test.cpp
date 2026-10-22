//===-- posix_fadvise syscall injection tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/types/off_t.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

#include <sys/syscall.h>

namespace {
long result;
int calls, seen_fd, seen_advice;
off_t seen_offset, seen_len;
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T fadvise_test_syscall(long number, int fd, off_t offset, off_t len,
                       int advice) {
  EXPECT_EQ(number, long(SYS_fadvise64));
  ++calls;
  seen_fd = fd;
  seen_offset = offset;
  seen_len = len;
  seen_advice = advice;
  return static_cast<T>(result);
}
} // namespace LIBC_NAMESPACE_DECL

#define syscall_impl fadvise_test_syscall
#define posix_fadvise injected_posix_fadvise
#include "src/fcntl/linux/posix_fadvise.cpp"
#undef posix_fadvise
#undef syscall_impl

TEST(LlvmLibcPosixFadviseSyscallTest, ForwardsAllAdviceAndFullWidthRanges) {
  const int advice_values[] = {POSIX_FADV_NORMAL,     POSIX_FADV_RANDOM,
                               POSIX_FADV_SEQUENTIAL, POSIX_FADV_WILLNEED,
                               POSIX_FADV_DONTNEED,   POSIX_FADV_NOREUSE};
  constexpr off_t max = LIBC_NAMESPACE::cpp::numeric_limits<off_t>::max();
  const off_t ranges[][2] = {{0, 0},
                             {(off_t(1) << 40) + 17, (off_t(1) << 41) + 31},
                             {max, 0},
                             {max, max}};
  for (int advice : advice_values) {
    for (const auto &range : ranges) {
      result = 0;
      calls = 0;
      LIBC_NAMESPACE::libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::injected_posix_fadvise(37, range[0], range[1],
                                                       advice),
                0);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_fd, 37);
      EXPECT_EQ(seen_offset, range[0]);
      EXPECT_EQ(seen_len, range[1]);
      EXPECT_EQ(seen_advice, advice);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    }
  }
}

TEST(LlvmLibcPosixFadviseSyscallTest, LeavesRangeAndAdviceValidationToKernel) {
  const off_t ranges[][2] = {{-1, 1}, {0, -1}, {0, 0}};
  for (const auto &range : ranges) {
    result = -EINVAL;
    calls = 0;
    LIBC_NAMESPACE::libc_errno = ERANGE;
    EXPECT_EQ(
        LIBC_NAMESPACE::injected_posix_fadvise(37, range[0], range[1], -1),
        EINVAL);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen_offset, range[0]);
    EXPECT_EQ(seen_len, range[1]);
    EXPECT_EQ(seen_advice, -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  }
}

TEST(LlvmLibcPosixFadviseSyscallTest, ReturnsErrorsDirectlyWithoutRetry) {
  const int errors[] = {EBADF, EINVAL, ESPIPE, ENOSYS, EOPNOTSUPP, EINTR, EIO};
  const int saved_errnos[] = {0, EDOM};
  for (int error : errors) {
    for (int saved_errno : saved_errnos) {
      result = -error;
      calls = 0;
      LIBC_NAMESPACE::libc_errno = saved_errno;
      EXPECT_EQ(
          LIBC_NAMESPACE::injected_posix_fadvise(-1, 0, 0, POSIX_FADV_NORMAL),
          error);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_fd, -1);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
    }
  }
}
