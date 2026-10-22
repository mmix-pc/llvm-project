//===-- MMIX fcntl cancellation dispatch tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "fcntl_test_syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
unsigned raw_calls, cancel_calls;
int reply, seen_fd, seen_cmd;
void *seen_arg;
} // namespace

namespace LIBC_NAMESPACE_DECL {
int fcntl_test_syscall(long number, int fd, int cmd, void *arg) {
  EXPECT_EQ(number, long(SYS_fcntl));
  ++raw_calls;
  seen_fd = fd;
  seen_cmd = cmd;
  seen_arg = arg;
  if (cmd == F_GETOWN_EX && reply >= 0) {
    auto *owner = static_cast<struct f_owner_ex *>(arg);
    owner->type = F_OWNER_PGRP;
    owner->pid = 123;
  }
  return reply;
}

namespace mmix {
long cancellation_syscall(unsigned long number, long fd, long cmd, long arg,
                          long a4, long a5, long a6) {
  EXPECT_EQ(number, static_cast<unsigned long>(SYS_fcntl));
  EXPECT_EQ(a4 | a5 | a6, 0L);
  ++cancel_calls;
  seen_fd = static_cast<int>(fd);
  seen_cmd = static_cast<int>(cmd);
  seen_arg = reinterpret_cast<void *>(arg);
  return reply;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#include "src/fcntl/linux/mmix/cancellation_fcntl.cpp"

TEST(LlvmLibcMmixFcntl, OnlyBlockingLocksEnterCancellation) {
  struct flock lock = {};
  const int commands[] = {F_SETLKW, F_OFD_SETLKW};
  const int errors[] = {0, EINTR, EBADF, EDEADLK};
  for (int cmd : commands) {
    for (int error : errors) {
      raw_calls = cancel_calls = 0;
      reply = -error;
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::fcntl(17, cmd, &lock), error ? -1 : 0);
      EXPECT_EQ(raw_calls, 0U);
      EXPECT_EQ(cancel_calls, 1U);
      EXPECT_EQ(seen_fd, 17);
      EXPECT_EQ(seen_cmd, cmd);
      EXPECT_EQ(seen_arg, static_cast<void *>(&lock));
      EXPECT_EQ(int(libc_errno), error ? error : EDOM);
    }
  }
}

TEST(LlvmLibcMmixFcntl, NonblockingCommandsPreserveGenericSemantics) {
  raw_calls = cancel_calls = 0;
  reply = 0;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, F_GETFD), 0);
  EXPECT_EQ(seen_arg, nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, F_SETOWN, -42), 0);
  EXPECT_EQ(seen_arg, reinterpret_cast<void *>(intptr_t(-42)));
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, F_GETOWN), -123);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(raw_calls, 3U);
  EXPECT_EQ(cancel_calls, 0U);

  reply = -EBADF;
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(-1, F_GETFL), -1);
  EXPECT_EQ(int(libc_errno), EBADF);
  EXPECT_EQ(raw_calls, 4U);
  EXPECT_EQ(cancel_calls, 0U);
}
