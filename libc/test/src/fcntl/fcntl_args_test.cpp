//===-- Tests for Linux fcntl argument forwarding -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "fcntl_test_syscall.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/types/struct_flock.h"
#include "src/fcntl/fcntl.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
long last_number;
int last_fd, last_cmd, reply;
unsigned calls;
void *last_arg;
struct flock64 last_lock;

class LlvmLibcFcntlArgsTest
    : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
public:
  void SetUp() override {
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::SetUp();
    last_number = last_fd = last_cmd = reply = 0;
    calls = 0;
    last_arg = nullptr;
    last_lock = {};
  }
};
} // namespace

namespace LIBC_NAMESPACE_DECL {
int fcntl_test_syscall(long number, int fd, int cmd, void *arg) {
  last_number = number;
  last_fd = fd;
  last_cmd = cmd;
  last_arg = arg;
  ++calls;
  if (reply < 0)
    return reply;
  if (cmd == F_GETOWN_EX) {
    auto *owner = static_cast<struct f_owner_ex *>(arg);
    owner->type = F_OWNER_PGRP;
    owner->pid = 17;
  }
  if (cmd == F_OFD_GETLK || cmd == F_OFD_SETLK || cmd == F_OFD_SETLKW)
    last_lock = *static_cast<struct flock64 *>(arg);
  return reply;
}
} // namespace LIBC_NAMESPACE_DECL

TEST_F(LlvmLibcFcntlArgsTest, QueriesIgnoreUnusedArgument) {
  // These extension values are Linux UAPI numbers, not host-header macros.
  const int commands[] = {F_GETFD, F_GETFL, F_GETSIG, 1025, 1032, 1034, 1028};
  int sentinel;
  for (int cmd : commands) {
    // The old unconditional va_arg read forwards this non-null pointer.
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, cmd, static_cast<void *>(&sentinel)), 0);
    EXPECT_EQ(last_arg, nullptr);
    EXPECT_EQ(last_fd, 7);
    EXPECT_EQ(last_cmd, cmd);
#ifdef SYS_fcntl
    EXPECT_EQ(last_number, long(SYS_fcntl));
#else
    EXPECT_EQ(last_number, long(SYS_fcntl64));
#endif
  }
  EXPECT_EQ(calls, unsigned(sizeof(commands) / sizeof(commands[0])));
}

TEST_F(LlvmLibcFcntlArgsTest, SignedIntegerArguments) {
  const int commands[] = {F_DUPFD, F_SETFD, F_SETFL, F_SETOWN, F_SETSIG,
                          1030,    1027,    1024,    1031,     1033};
  for (int cmd : commands) {
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, cmd, -42), 0);
    EXPECT_EQ(last_arg, reinterpret_cast<void *>(intptr_t(-42)));
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, cmd, 0), 0);
    EXPECT_EQ(last_arg, nullptr);
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, cmd, 42), 0);
    EXPECT_EQ(last_arg, reinterpret_cast<void *>(intptr_t(42)));
  }
}

TEST_F(LlvmLibcFcntlArgsTest, NotificationUsesLong) {
  long mask = static_cast<long>(0x80000000UL);
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, 1026, mask), 0);
  EXPECT_EQ(last_arg, reinterpret_cast<void *>(mask));
}

TEST_F(LlvmLibcFcntlArgsTest, LockPointers) {
  struct flock lock = {};
  const int commands[] = {F_GETLK, F_SETLK, F_SETLKW};
  for (int cmd : commands) {
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, cmd, &lock), 0);
    EXPECT_EQ(last_arg, static_cast<void *>(&lock));
  }
#if defined(F_GETLK64) && F_GETLK64 != F_GETLK
  struct flock64 lock64 = {};
  const int commands64[] = {F_GETLK64, F_SETLK64, F_SETLKW64};
  for (int cmd : commands64) {
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, cmd, &lock64), 0);
    EXPECT_EQ(last_arg, static_cast<void *>(&lock64));
  }
#endif
  lock.l_type = F_WRLCK;
  lock.l_start = 123;
  lock.l_len = 456;
  const int ofd_commands[] = {F_OFD_GETLK, F_OFD_SETLK, F_OFD_SETLKW};
  for (int cmd : ofd_commands) {
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, cmd, &lock), 0);
    EXPECT_EQ(last_lock.l_type, lock.l_type);
    EXPECT_EQ(last_lock.l_start, lock.l_start);
    EXPECT_EQ(last_lock.l_len, lock.l_len);
  }
}

TEST_F(LlvmLibcFcntlArgsTest, OwnerPointersAndNegativeOwner) {
  struct f_owner_ex owner = {};
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, F_GETOWN_EX, &owner), 0);
  EXPECT_EQ(last_arg, static_cast<void *>(&owner));
  EXPECT_EQ(owner.pid, 17);
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, F_SETOWN_EX, &owner), 0);
  EXPECT_EQ(last_arg, static_cast<void *>(&owner));
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, F_GETOWN), -17);
  EXPECT_EQ(last_cmd, F_GETOWN_EX);
  ASSERT_ERRNO_SUCCESS();
}

TEST_F(LlvmLibcFcntlArgsTest, ErrorAndUnknownPointerPassthrough) {
  reply = -EBADF;
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, F_GETFD), -1);
  ASSERT_ERRNO_EQ(EBADF);
  int value;
  void *ptr = &value;
  reply = -EINVAL;
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(7, -1, ptr), -1);
  EXPECT_EQ(last_arg, ptr);
  EXPECT_EQ(last_cmd, -1);
  ASSERT_ERRNO_EQ(EINVAL);
}
