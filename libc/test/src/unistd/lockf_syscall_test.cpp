//===-- Tests for lockf fcntl translation ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/types/off_t.h"
#include "hdr/types/struct_flock.h"
#include "hdr/unistd_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/unistd/getpid.h"
#include "test/UnitTest/Test.h"

#include <stdarg.h>

namespace {
constexpr pid_t SELF = 123;
int calls, pid_calls, seen_fd, seen_cmd, reply_errno;
struct flock seen_lock;
short reply_type;
pid_t reply_pid;

constexpr struct {
  int function;
  int command;
  short type;
} OPERATIONS[] = {{F_ULOCK, F_SETLK, F_UNLCK},
                  {F_LOCK, F_SETLKW, F_WRLCK},
                  {F_TLOCK, F_SETLK, F_WRLCK},
                  {F_TEST, F_GETLK, F_RDLCK}};

class LlvmLibcLockfSyscallTest : public LIBC_NAMESPACE::testing::Test {
protected:
  void SetUp() override {
    calls = pid_calls = reply_errno = 0;
    seen_fd = seen_cmd = -1;
    seen_lock = {};
    reply_type = F_UNLCK;
    reply_pid = SELF + 1;
    LIBC_NAMESPACE::libc_errno = EDOM;
  }
};
} // namespace

namespace LIBC_NAMESPACE_DECL {
int lockf_test_fcntl(int fd, int cmd, ...) {
  va_list args;
  va_start(args, cmd);
  auto *lock = va_arg(args, struct flock *);
  va_end(args);
  ++calls;
  seen_fd = fd;
  seen_cmd = cmd;
  seen_lock = *lock;
  if (reply_errno != 0) {
    libc_errno = reply_errno;
    return -1;
  }
  if (cmd == F_GETLK) {
    lock->l_type = reply_type;
    lock->l_pid = reply_pid;
  }
  return 0;
}

pid_t lockf_test_getpid() {
  ++pid_calls;
  return SELF;
}
} // namespace LIBC_NAMESPACE_DECL

// Inject at the generic fcntl boundary, leaving the runner's syscalls intact.
#define fcntl lockf_test_fcntl
#define getpid lockf_test_getpid
#define lockf injected_lockf
#include "src/unistd/lockf.cpp"
#undef lockf
#undef getpid
#undef fcntl

TEST_F(LlvmLibcLockfSyscallTest, CommandsAndSignedCurrentPositionRanges) {
  constexpr off_t max = LIBC_NAMESPACE::cpp::numeric_limits<off_t>::max();
  constexpr off_t min = LIBC_NAMESPACE::cpp::numeric_limits<off_t>::min();
  const off_t lengths[] = {0, 1, -1, 37, -37, max / 2, -max / 2, max, min};
  for (const auto &operation : OPERATIONS) {
    for (off_t len : lengths) {
      calls = pid_calls = 0;
      LIBC_NAMESPACE::libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::injected_lockf(17, operation.function, len), 0);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_fd, 17);
      EXPECT_EQ(seen_cmd, operation.command);
      EXPECT_EQ(seen_lock.l_type, operation.type);
      EXPECT_EQ(int(seen_lock.l_whence), SEEK_CUR);
      EXPECT_EQ(seen_lock.l_start, off_t(0));
      EXPECT_EQ(seen_lock.l_len, len);
      EXPECT_EQ(seen_lock.l_pid, pid_t(0));
      EXPECT_EQ(pid_calls, 0);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    }
  }
}

TEST_F(LlvmLibcLockfSyscallTest, PreservesFcntlErrorsWithoutRetry) {
  const int errors[] = {EBADF,  EACCES, EAGAIN,    EDEADLK,    EINTR,
                        EINVAL, ENOLCK, EOVERFLOW, EOPNOTSUPP, EIO};
  for (const auto &operation : OPERATIONS) {
    for (int error : errors) {
      calls = pid_calls = 0;
      reply_errno = error;
      // An error must win even if the reply would otherwise mean unlocked.
      reply_type = F_UNLCK;
      reply_pid = SELF;
      LIBC_NAMESPACE::libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::injected_lockf(-1, operation.function, -37),
                -1);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_fd, -1);
      EXPECT_EQ(seen_cmd, operation.command);
      EXPECT_EQ(seen_lock.l_len, off_t(-37));
      EXPECT_EQ(pid_calls, 0);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    }
  }
}

TEST_F(LlvmLibcLockfSyscallTest, TestAcceptsUnlockedOrOwnLock) {
  const short types[] = {F_UNLCK, F_RDLCK, F_WRLCK};
  for (short type : types) {
    calls = pid_calls = 0;
    reply_type = type;
    reply_pid = SELF;
    EXPECT_EQ(LIBC_NAMESPACE::injected_lockf(17, F_TEST, 37), 0);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen_cmd, F_GETLK);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  }
}

TEST_F(LlvmLibcLockfSyscallTest, TestReportsOtherOwnerAsAccessDenied) {
  const pid_t owners[] = {SELF + 1, -1};
  const short types[] = {F_RDLCK, F_WRLCK};
  for (pid_t owner : owners) {
    for (short type : types) {
      calls = pid_calls = 0;
      reply_type = type;
      reply_pid = owner;
      LIBC_NAMESPACE::libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::injected_lockf(17, F_TEST, 0), -1);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_cmd, F_GETLK);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EACCES);
    }
  }
}

TEST_F(LlvmLibcLockfSyscallTest, InvalidCommandDoesNotCallFcntl) {
  const int commands[] = {-1, LIBC_NAMESPACE::cpp::numeric_limits<int>::min(),
                          LIBC_NAMESPACE::cpp::numeric_limits<int>::max()};
  for (int cmd : commands) {
    LIBC_NAMESPACE::libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::injected_lockf(-1, cmd, 0), -1);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(pid_calls, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  }
}
