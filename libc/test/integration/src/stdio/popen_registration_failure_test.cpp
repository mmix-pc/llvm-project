//===-- Pipe stream fork-registration failure cleanup ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/stdio/linux/pipe_fork.h"
#include "src/stdio/popen.h"
#include "src/unistd/close.h"
#include "test/IntegrationTest/test.h"
#include <fcntl.h>

TEST_MAIN() {
  // Exhaust the real selected registry in this isolated process; no mock
  // failure path and no dependence on its exact capacity.
  unsigned count = 0;
  while (
      LIBC_NAMESPACE::register_pipe_fork_callbacks(nullptr, nullptr, nullptr))
    ASSERT_TRUE(++count < 1024);
  int flags = LIBC_NAMESPACE::fcntl(2, F_GETFD);
  ASSERT_TRUE(flags >= 0);
  int next_fd = LIBC_NAMESPACE::fcntl(2, F_DUPFD, 3);
  ASSERT_TRUE(next_fd >= 3);
  ASSERT_EQ(LIBC_NAMESPACE::close(next_fd), 0);
  for (int i = 0; i < 4; ++i) {
    ASSERT_EQ(LIBC_NAMESPACE::popen("exit 99", "r"), nullptr);
    ASSERT_EQ(libc_errno, ENOMEM);
    ASSERT_EQ(LIBC_NAMESPACE::fcntl(2, F_GETFD), flags);
    int fd = LIBC_NAMESPACE::fcntl(2, F_DUPFD, 3);
    ASSERT_EQ(fd, next_fd);
    ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
  }
  return 0;
}
