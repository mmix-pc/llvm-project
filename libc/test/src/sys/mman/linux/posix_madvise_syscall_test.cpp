//===-- posix_madvise syscall boundary tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/sys_mman_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

#include <sys/syscall.h>

namespace {
int calls, result, seen_advice;
long seen_address;
size_t seen_size;
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T madvise_test_syscall(long number, long address, size_t size, int advice) {
  EXPECT_EQ(number, long(SYS_madvise));
  ++calls;
  seen_address = address;
  seen_size = size;
  seen_advice = advice;
  return static_cast<T>(result);
}
} // namespace LIBC_NAMESPACE_DECL

#define syscall_impl madvise_test_syscall
#define posix_madvise injected_posix_madvise
#include "src/sys/mman/linux/posix_madvise.cpp"
#undef posix_madvise
#undef syscall_impl

TEST(LlvmLibcPosixMadviseSyscallTest, ForwardsNonDestructiveAdvice) {
  const int values[] = {POSIX_MADV_NORMAL, POSIX_MADV_RANDOM,
                        POSIX_MADV_SEQUENTIAL, POSIX_MADV_WILLNEED};
  const int errors[] = {0, EINVAL, ENOMEM, EINTR};
  char data;
  for (int advice : values) {
    for (int error : errors) {
      calls = 0;
      result = -error;
      LIBC_NAMESPACE::libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::injected_posix_madvise(&data, size_t(-1), advice),
                error);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_address, reinterpret_cast<long>(&data));
      EXPECT_EQ(seen_size, size_t(-1));
      EXPECT_EQ(seen_advice, advice);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    }
  }
}

TEST(LlvmLibcPosixMadviseSyscallTest, DontNeedDoesNotDiscardPages) {
  calls = 0;
  result = -ENOMEM;
  LIBC_NAMESPACE::libc_errno = ERANGE;
  EXPECT_EQ(LIBC_NAMESPACE::injected_posix_madvise(nullptr, size_t(-1),
                                                 POSIX_MADV_DONTNEED),
            0);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
}

TEST(LlvmLibcPosixMadviseSyscallTest, InvalidAdviceNeverReachesKernel) {
  const int values[] = {-1, MADV_REMOVE, MADV_FREE, MADV_DONTFORK, 0x7fffffff};
  for (int advice : values) {
    calls = 0;
    result = 0;
    LIBC_NAMESPACE::libc_errno = ERANGE;
    EXPECT_EQ(LIBC_NAMESPACE::injected_posix_madvise(nullptr, 0, advice), EINVAL);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  }
}
