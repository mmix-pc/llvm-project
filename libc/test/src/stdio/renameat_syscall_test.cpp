//===-- renameat syscall injection tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/renameat.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
constexpr long RENAMEAT_NUMBER = 1001;
constexpr long RENAMEAT2_NUMBER = 1002;
int result, calls, seen_oldfd, seen_newfd, seen_flags;
long seen_number;
const char *seen_oldpath, *seen_newpath;
} // namespace

namespace LIBC_NAMESPACE_DECL {
int injected_renameat(int, const char *, int, const char *);
int injected_renameat2(int, const char *, int, const char *);

template <typename T>
T renameat_test_syscall(long number, int oldfd, const char *oldpath, int newfd,
                        const char *newpath, int flags = -1) {
  ++calls;
  seen_number = number;
  seen_oldfd = oldfd;
  seen_oldpath = oldpath;
  seen_newfd = newfd;
  seen_newpath = newpath;
  seen_flags = flags;
  return static_cast<T>(result);
}
} // namespace LIBC_NAMESPACE_DECL

// Synthetic numbers never reach the kernel. Compile both production branches
// independently of which syscalls the test host's headers provide.
#pragma push_macro("SYS_renameat")
#pragma push_macro("SYS_renameat2")
#undef SYS_renameat
#undef SYS_renameat2
#define SYS_renameat RENAMEAT_NUMBER
#define SYS_renameat2 RENAMEAT2_NUMBER
#define syscall_impl renameat_test_syscall
#define renameat injected_renameat
#include "src/stdio/linux/renameat.cpp"
#undef renameat
#undef SYS_renameat
#define renameat injected_renameat2
#include "src/stdio/linux/renameat.cpp"
#undef renameat
#undef syscall_impl
#pragma pop_macro("SYS_renameat2")
#pragma pop_macro("SYS_renameat")

TEST(LlvmLibcRenameatSyscallTest, BothBranchesForwardPathsAndDescriptors) {
  const char oldpath[] = "source", newpath[] = "destination";
  const bool branches[] = {false, true};
  for (bool use_renameat2 : branches) {
    auto provider = use_renameat2 ? LIBC_NAMESPACE::injected_renameat2
                                  : LIBC_NAMESPACE::injected_renameat;
    result = 0;
    calls = 0;
    LIBC_NAMESPACE::libc_errno = EDOM;
    EXPECT_EQ(provider(37, oldpath, 42, newpath), 0);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen_number, use_renameat2 ? RENAMEAT2_NUMBER : RENAMEAT_NUMBER);
    EXPECT_EQ(seen_flags, use_renameat2 ? 0 : -1);
    EXPECT_EQ(seen_oldfd, 37);
    EXPECT_EQ(seen_newfd, 42);
    EXPECT_EQ(seen_oldpath, oldpath);
    EXPECT_EQ(seen_newpath, newpath);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);

    EXPECT_EQ(provider(AT_FDCWD, oldpath, AT_FDCWD, newpath), 0);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(seen_oldfd, AT_FDCWD);
    EXPECT_EQ(seen_newfd, AT_FDCWD);
  }
}

TEST(LlvmLibcRenameatSyscallTest, BothBranchesPropagateErrorsWithoutRetry) {
  const int errors[] = {EBADF, EACCES, ENOENT, ENOTDIR, ENOTEMPTY, EINVAL,
                        EXDEV, EROFS,  EIO,    EINTR,   ENOSYS};
  const bool branches[] = {false, true};
  for (bool use_renameat2 : branches) {
    auto provider = use_renameat2 ? LIBC_NAMESPACE::injected_renameat2
                                  : LIBC_NAMESPACE::injected_renameat;
    for (int error : errors) {
      result = -error;
      calls = 0;
      LIBC_NAMESPACE::libc_errno = 0;
      EXPECT_EQ(provider(-1, nullptr, -2, nullptr), -1);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_oldfd, -1);
      EXPECT_EQ(seen_newfd, -2);
      EXPECT_EQ(seen_oldpath, nullptr);
      EXPECT_EQ(seen_newpath, nullptr);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    }
  }
}
