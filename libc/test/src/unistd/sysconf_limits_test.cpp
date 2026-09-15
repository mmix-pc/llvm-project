//===-- Tests for Linux sysconf limits and errors -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/sys_resource_macros.h"
#include "hdr/unistd_macros.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/attributes.h"
#include "src/unistd/sysconf.h"
#include "sysconf_test_syscall.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
uint64_t limit_value;
int query_error, last_resource;
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {

ErrorOr<int> sysconf_test_prlimit(long number, pid_t pid, int resource,
                                  const struct rlimit *new_limit,
                                  struct rlimit *old_limit) {
  if (number != SYS_prlimit64 || pid != 0 || new_limit || !old_limit)
    return Error(EINVAL);
  last_resource = resource;
  if (query_error)
    return Error(query_error);
  // prlimit64 writes two 64-bit fields even in a 32-bit overlay build.
  uint64_t limits[] = {limit_value, limit_value};
  __builtin_memcpy(old_limit, limits, sizeof(limits));
  return 0;
}

ErrorOr<int> sysconf_test_sysinfo(long number, struct sysinfo *info) {
  if (number != SYS_sysinfo || !info)
    return Error(EINVAL);
  return Error(query_error ? query_error : EIO);
}

} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcSysconfLimitsTest, ArgumentBounds) {
  query_error = 0;
  const struct {
    uint64_t stack;
    long expected;
  } cases[] = {{0, 131072},
               {512 * 1024, 131072},
               {4 * 1024 * 1024, 1024 * 1024},
               {24 * 1024 * 1024, 6 * 1024 * 1024},
               {64 * 1024 * 1024, 6 * 1024 * 1024},
               {UINT64_MAX - 1, 6 * 1024 * 1024},
               {UINT64_MAX, 6 * 1024 * 1024}};
  for (auto test : cases) {
    limit_value = test.stack;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_ARG_MAX), test.expected);
    EXPECT_EQ(last_resource, int(RLIMIT_STACK));
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST(LlvmLibcSysconfLimitsTest, PositiveErrno) {
  const int errors[] = {EACCES, EIO};
  const int names[] = {_SC_ARG_MAX, _SC_OPEN_MAX, _SC_PHYS_PAGES};
  for (int error : errors) {
    query_error = error;
    for (int name : names) {
      libc_errno = 0;
      EXPECT_EQ(LIBC_NAMESPACE::sysconf(name), -1L);
      EXPECT_EQ(int(libc_errno), error);
    }
  }
  query_error = 0;
}

TEST(LlvmLibcSysconfLimitsTest, OpenLimit) {
  query_error = 0;
  limit_value = 1024;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX), 1024L);
  EXPECT_EQ(last_resource, int(RLIMIT_NOFILE));
  EXPECT_EQ(int(libc_errno), EDOM);
  limit_value = UINT64_MAX;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX), -1L);
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST(LlvmLibcSysconfLimitsTest, PublicThreadCapability) {
  libc_errno = EDOM;
  // Internal mutex selection must not override the public capability.
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREADS), long(_POSIX_THREADS));
  EXPECT_EQ(int(libc_errno), EDOM);
}
