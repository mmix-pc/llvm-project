//===-- fchmodat syscall injection tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/types/mode_t.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

// Exercise old headers and architectures without the legacy syscall using
// only syscall numbers supplied by the build's actual UAPI.
#ifdef LIBC_TEST_FCHMODAT_NO_MODERN
#undef SYS_fchmodat2
#endif
#ifdef LIBC_TEST_FCHMODAT_NO_LEGACY
#undef SYS_fchmodat
#endif

namespace {
int modern_result, legacy_result, modern_calls, legacy_calls;
constexpr int FD = 37;
constexpr mode_t MODE = 0640;
const char PATH[] = "relative/path";
int expected_flags;

void reset(int modern = 0, int legacy = 0) {
  modern_result = modern;
  legacy_result = legacy;
  modern_calls = legacy_calls = 0;
  LIBC_NAMESPACE::libc_errno = EDOM;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T fchmodat_test_syscall(long number, int fd, const char *path, mode_t mode,
                        int flags) {
#ifdef SYS_fchmodat2
  EXPECT_EQ(number, long(SYS_fchmodat2));
#else
  (void)number;
  EXPECT_TRUE(false);
#endif
  ++modern_calls;
  EXPECT_EQ(fd, FD);
  EXPECT_EQ(path, PATH);
  EXPECT_EQ(mode, MODE);
  EXPECT_EQ(flags, expected_flags);
  return static_cast<T>(modern_result);
}

template <typename T>
T fchmodat_test_syscall(long number, int fd, const char *path, mode_t mode) {
#ifdef SYS_fchmodat
  EXPECT_EQ(number, long(SYS_fchmodat));
#else
  (void)number;
  EXPECT_TRUE(false);
#endif
  ++legacy_calls;
  EXPECT_EQ(fd, FD);
  EXPECT_EQ(path, PATH);
  EXPECT_EQ(mode, MODE);
  return static_cast<T>(legacy_result);
}
} // namespace LIBC_NAMESPACE_DECL

// Include the real public provider without intercepting the runner's syscalls.
#define syscall_impl fchmodat_test_syscall
#define fchmodat injected_fchmodat
#include "src/sys/stat/linux/fchmodat.cpp"
#undef fchmodat
#undef syscall_impl

TEST(LlvmLibcFchmodatSyscallTest, InvalidFlagsNeverReachKernel) {
  const int invalid_flags[] = {AT_SYMLINK_FOLLOW, AT_REMOVEDIR, -1,
                               AT_SYMLINK_NOFOLLOW | AT_SYMLINK_FOLLOW,
                               AT_EMPTY_PATH | AT_REMOVEDIR};
  for (int flags : invalid_flags) {
    reset();
    EXPECT_EQ(LIBC_NAMESPACE::injected_fchmodat(FD, PATH, MODE, flags), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
    EXPECT_EQ(modern_calls, 0);
    EXPECT_EQ(legacy_calls, 0);
  }
}

#ifdef SYS_fchmodat2
TEST(LlvmLibcFchmodatSyscallTest, ModernSuccessPreservesFlagsAndErrno) {
  const int flags[] = {0, AT_SYMLINK_NOFOLLOW, AT_EMPTY_PATH,
                       AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH};
  for (int flag : flags) {
    reset();
    expected_flags = flag;
    EXPECT_EQ(LIBC_NAMESPACE::injected_fchmodat(FD, PATH, MODE, flag), 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    EXPECT_EQ(modern_calls, 1);
    EXPECT_EQ(legacy_calls, 0);
  }
}

TEST(LlvmLibcFchmodatSyscallTest, ModernErrorsDoNotFallBack) {
  const int errors[] = {EBADF,      EACCES, EPERM, ENOENT, EINVAL,
                        EOPNOTSUPP, EROFS,  EINTR, EIO};
  const int flags[] = {0, AT_SYMLINK_NOFOLLOW, AT_EMPTY_PATH};
  for (int error : errors) {
    for (int flag : flags) {
      reset(-error);
      expected_flags = flag;
      EXPECT_EQ(LIBC_NAMESPACE::injected_fchmodat(FD, PATH, MODE, flag), -1);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
      EXPECT_EQ(modern_calls, 1);
      EXPECT_EQ(legacy_calls, 0);
    }
  }
}
#endif

TEST(LlvmLibcFchmodatSyscallTest, UnavailableModernNeverDiscardsFlags) {
  const int flags[] = {AT_SYMLINK_NOFOLLOW, AT_EMPTY_PATH,
                       AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH};
  for (int flag : flags) {
    reset(-ENOSYS);
    expected_flags = flag;
    EXPECT_EQ(LIBC_NAMESPACE::injected_fchmodat(FD, PATH, MODE, flag), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EOPNOTSUPP);
#ifdef SYS_fchmodat2
    EXPECT_EQ(modern_calls, 1);
#else
    EXPECT_EQ(modern_calls, 0);
#endif
    EXPECT_EQ(legacy_calls, 0);
  }
}

TEST(LlvmLibcFchmodatSyscallTest, ZeroFlagsLegacyFallback) {
  const int results[] = {0,       -EBADF, -EACCES, -EPERM, -ENOENT,
                         -EINVAL, -EROFS, -EINTR,  -EIO,   -ENOSYS};
  for (int result : results) {
    reset(-ENOSYS, result);
    expected_flags = 0;
#ifdef SYS_fchmodat
    EXPECT_EQ(LIBC_NAMESPACE::injected_fchmodat(FD, PATH, MODE, 0),
              result == 0 ? 0 : -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), result == 0 ? EDOM : -result);
    EXPECT_EQ(legacy_calls, 1);
#else
    EXPECT_EQ(LIBC_NAMESPACE::injected_fchmodat(FD, PATH, MODE, 0), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOSYS);
    EXPECT_EQ(legacy_calls, 0);
#endif
#ifdef SYS_fchmodat2
    EXPECT_EQ(modern_calls, 1);
#else
    EXPECT_EQ(modern_calls, 0);
#endif
  }
}
