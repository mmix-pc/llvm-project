//===-- MMIX positioned-write syscall policy tests ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/struct_iovec.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "test/UnitTest/Test.h"
#include <linux/fs.h>
#include <sys/syscall.h>

namespace {
long reply, seen_offset;
unsigned calls;
int seen_fd;
iovec seen_vector;
long observe(long number, int fd, const iovec *vector, long length, long offset,
             long high, long flags) {
  ++calls;
  EXPECT_EQ(number, long(SYS_pwritev2));
  EXPECT_EQ(length, 1L);
  EXPECT_EQ(high, 0L);
  EXPECT_EQ(flags, long(RWF_NOAPPEND));
  seen_fd = fd;
  seen_vector = *vector;
  seen_offset = offset;
  return reply;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T, typename... Args>
T pwrite_test_syscall(long number, Args... args) {
  return observe(number, args...);
}
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
namespace mmix {
long cancellation_syscall(unsigned long number, long fd, long vector,
                          long length, long offset, long high, long flags) {
  return observe(number, static_cast<int>(fd), reinterpret_cast<const iovec *>(vector), length,
                 offset, high, flags);
}
} // namespace mmix
#endif
} // namespace LIBC_NAMESPACE_DECL

#define syscall_impl pwrite_test_syscall
#define pwrite injected_pwrite
#include "src/unistd/linux/mmix/pwrite.cpp"
#undef pwrite
#undef syscall_impl

TEST(LlvmLibcMmixPwrite, RejectsCurrentOffsetSentinelWithoutSyscall) {
  const off_t offsets[] = {-1, -2};
  for (off_t offset : offsets) {
    calls = 0;
    EXPECT_EQ(LIBC_NAMESPACE::injected_pwrite(7, nullptr, 0, offset), ssize_t(-1));
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(calls, 0U);
  }
}

TEST(LlvmLibcMmixPwrite, PreservesFullOffsetShortProgressAndErrors) {
  char buffer[8] = {};
  constexpr off_t offset = 0x123456789L;
  const long results[] = {0, 3, 8, -EINTR, -EOPNOTSUPP, -ENOSYS, -EFBIG};
  for (long result : results) {
    reply = result;
    calls = 0;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::injected_pwrite(17, buffer, sizeof(buffer), offset),
              result < 0 ? -1L : result);
    EXPECT_EQ(int(libc_errno), result < 0 ? int(-result) : EDOM);
    EXPECT_EQ(calls, 1U);
    EXPECT_EQ(seen_fd, 17);
    EXPECT_EQ(seen_offset, offset);
    EXPECT_EQ(seen_vector.iov_base, static_cast<void *>(buffer));
    EXPECT_EQ(seen_vector.iov_len, sizeof(buffer));
  }
}
