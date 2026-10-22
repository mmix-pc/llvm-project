//===-- MMIX msync cancellation transport tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/sys_mman_macros.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
long reply, seen_address, seen_length, seen_flags;
unsigned calls;
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
long cancellation_syscall(unsigned long number, long address, long length,
                          long flags, long a4, long a5, long a6) {
  EXPECT_EQ(number, static_cast<unsigned long>(SYS_msync));
  EXPECT_EQ(a4 | a5 | a6, 0L);
  ++calls;
  seen_address = address;
  seen_length = length;
  seen_flags = flags;
  return reply;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#include "src/sys/mman/linux/mmix/cancellation_msync.cpp"

TEST(LlvmLibcMmixMsync, PreservesArgumentsAndKernelErrors) {
  const int flags[] = {MS_SYNC, MS_ASYNC, MS_SYNC | MS_INVALIDATE, -1};
  const int errors[] = {0, EINVAL, ENOMEM, EINTR};
  char data;
  for (int flag : flags) {
    for (int error : errors) {
      calls = 0;
      reply = -error;
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::msync(&data, size_t(-1), flag), error ? -1 : 0);
      EXPECT_EQ(calls, 1U);
      EXPECT_EQ(seen_address, reinterpret_cast<long>(&data));
      EXPECT_EQ(static_cast<size_t>(seen_length), size_t(-1));
      EXPECT_EQ(seen_flags, long(flag));
      EXPECT_EQ(int(libc_errno), error ? error : EDOM);
    }
  }
}
