//===-- fexecve syscall injection tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
struct {
  int result, fd;
  unsigned calls;
  char *const *argv;
  char *const *envp;
} state;

void reset(int fd, char *const argv[], char *const envp[], int result = 0,
           int saved_errno = EDOM) {
  state = {};
  state.fd = fd;
  state.argv = argv;
  state.envp = envp;
  state.result = result;
  LIBC_NAMESPACE::libc_errno = saved_errno;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T fexecve_test_syscall(long number, int fd, const char *path, char *const argv[],
                     char *const envp[], int flags) {
  EXPECT_EQ(number, long(SYS_execveat));
  EXPECT_EQ(fd, state.fd);
  EXPECT_STREQ(path, "");
  EXPECT_EQ(argv, state.argv);
  EXPECT_EQ(envp, state.envp);
  EXPECT_EQ(flags, AT_EMPTY_PATH);
  EXPECT_EQ(++state.calls, 1U);
  // Bound an erroneous EINTR retry loop without issuing any host syscall.
  if (state.calls > 1)
    return static_cast<T>(-EIO);
  return static_cast<T>(state.result);
}
} // namespace LIBC_NAMESPACE_DECL

// Exercise the production provider even when its public export is unselected.
#define syscall_impl fexecve_test_syscall
#define fexecve injected_fexecve
#include "src/unistd/linux/fexecve.cpp"
#undef fexecve
#undef syscall_impl

TEST(LlvmLibcFexecveSyscallTest, ForwardsArgumentsAndPreservesErrnoOnReturn) {
  char name[] = "program";
  char argument[] = "argument";
  char variable[] = "KEY=value";
  char *argv[] = {name, argument, nullptr};
  char *envp[] = {variable, nullptr};
  const int descriptors[] = {0, 37,
                            LIBC_NAMESPACE::cpp::numeric_limits<int>::max()};
  const int saved_errnos[] = {0, EDOM};
  for (int fd : descriptors) {
    for (int saved_errno : saved_errnos) {
      reset(fd, argv, envp, 0, saved_errno);
      // A real successful exec cannot return; this checks only wrapper behavior.
      EXPECT_EQ(LIBC_NAMESPACE::injected_fexecve(fd, argv, envp), 0);
      EXPECT_EQ(state.calls, 1U);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
      EXPECT_EQ(argv[0], name);
      EXPECT_EQ(argv[1], argument);
      EXPECT_EQ(argv[2], nullptr);
      EXPECT_EQ(envp[0], variable);
      EXPECT_EQ(envp[1], nullptr);
      EXPECT_STREQ(name, "program");
      EXPECT_STREQ(argument, "argument");
      EXPECT_STREQ(variable, "KEY=value");
    }
  }
}

TEST(LlvmLibcFexecveSyscallTest, NegativeDescriptorsFailWithEBADF) {
  char name[] = "program";
  char *argv[] = {name, nullptr};
  char *envp[] = {nullptr};
  const int descriptors[] = {-1, AT_FDCWD,
                            LIBC_NAMESPACE::cpp::numeric_limits<int>::min()};
  for (int fd : descriptors) {
    reset(fd, argv, envp);
    EXPECT_EQ(LIBC_NAMESPACE::injected_fexecve(fd, argv, envp), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
    EXPECT_EQ(state.calls, 0U);
  }
}

TEST(LlvmLibcFexecveSyscallTest, PropagatesErrorsWithoutRetryOrFallback) {
  char name[] = "program";
  char *argv[] = {name, nullptr};
  char *envp[] = {nullptr};
  const int errors[] = {EBADF,   EACCES, EPERM,   ENOENT,
                       ENOEXEC, ENOSYS, EINTR,   E2BIG,
                       ENOMEM,  EFAULT, EINVAL,  EIO,
                       ETXTBSY, ELOOP,  ENOTDIR, ENAMETOOLONG};
  for (int error : errors) {
    reset(37, argv, envp, -error);
    EXPECT_EQ(LIBC_NAMESPACE::injected_fexecve(37, argv, envp), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.calls, 1U);
  }
}

TEST(LlvmLibcFexecveSyscallTest, LeavesVectorValidationToKernel) {
  reset(37, nullptr, nullptr, -EFAULT);
  EXPECT_EQ(LIBC_NAMESPACE::injected_fexecve(37, nullptr, nullptr), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EFAULT);
  EXPECT_EQ(state.calls, 1U);
}
