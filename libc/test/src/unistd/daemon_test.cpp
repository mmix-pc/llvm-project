// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/sys/wait/waitpid.h"
#include "src/unistd/_exit.h"
#include "src/unistd/close.h"
#include "src/unistd/daemon.h"
#include "src/unistd/fork.h"
#include "src/unistd/getpid.h"
#include "src/unistd/getsid.h"
#include "src/unistd/pipe.h"
#include "src/unistd/read.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"
#include <sys/wait.h>
TEST(LlvmLibcDaemon, PreservedDescriptorsAndNewSession) {
  int pipefd[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe(pipefd), 0);
  pid_t child = LIBC_NAMESPACE::fork();
  ASSERT_GE(child, pid_t(0));
  if (!child) {
    LIBC_NAMESPACE::close(pipefd[0]);
    int ok = LIBC_NAMESPACE::daemon(1, 1) == 0 &&
             LIBC_NAMESPACE::getsid(0) == LIBC_NAMESPACE::getpid();
    LIBC_NAMESPACE::write(pipefd[1], &ok, sizeof(ok));
    LIBC_NAMESPACE::_exit(ok ? 0 : 99);
  }
  LIBC_NAMESPACE::close(pipefd[1]);
  int status;
  ASSERT_EQ(LIBC_NAMESPACE::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  int ok = 0;
  EXPECT_EQ(LIBC_NAMESPACE::read(pipefd[0], &ok, sizeof(ok)),
            ssize_t(sizeof(ok)));
  EXPECT_EQ(ok, 1);
  EXPECT_EQ(LIBC_NAMESPACE::close(pipefd[0]), 0);
}
