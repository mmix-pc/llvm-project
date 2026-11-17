// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/sys/wait/wait3.h"
#include "src/unistd/setpgrp.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <sys/wait.h>
#include "src/unistd/fork.h"
#include "src/unistd/getpgrp.h"
#include "src/unistd/getpid.h"
#include "src/unistd/_exit.h"
#include "src/unistd/setsid.h"
#include <unistd.h>

TEST(LlvmLibcProcessCompatibility, ChildGroupAndReaping) {
  pid_t child = LIBC_NAMESPACE::fork();
  ASSERT_GE(child, pid_t(0));
  if (!child) {
    libc_errno = EDOM;
    bool success = LIBC_NAMESPACE::setpgrp() == 0 &&
                   LIBC_NAMESPACE::getpgrp() == LIBC_NAMESPACE::getpid() && libc_errno == EDOM;
    LIBC_NAMESPACE::_exit(success ? 23 : 99);
  }
  int status;
  rusage usage;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::wait3(&status, 0, &usage), child);
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 23);
  EXPECT_GE(usage.ru_utime.tv_sec, 0L);
  EXPECT_GE(usage.ru_stime.tv_sec, 0L);
  EXPECT_EQ(LIBC_NAMESPACE::wait3(&status, WNOHANG, nullptr), pid_t(-1));
  EXPECT_EQ(int(libc_errno), ECHILD);
}

TEST(LlvmLibcProcessCompatibility, SessionLeaderRejection) {
  pid_t child = LIBC_NAMESPACE::fork();
  ASSERT_GE(child, pid_t(0));
  if (!child) {
    if (LIBC_NAMESPACE::setsid() < 0)
      LIBC_NAMESPACE::_exit(98);
    libc_errno = 0;
    LIBC_NAMESPACE::_exit(LIBC_NAMESPACE::setpgrp() == -1 && libc_errno == EPERM ? 24 : 99);
  }
  int status;
  ASSERT_EQ(LIBC_NAMESPACE::wait3(&status, 0, nullptr), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 24);
}
