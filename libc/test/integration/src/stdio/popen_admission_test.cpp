//===-- MMIX popen admission failure cleanup ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/stdio/popen.h"
#include "src/unistd/close.h"
#include "test/IntegrationTest/test.h"
#include <fcntl.h>

TEST_MAIN() {
  // This target is selected only for the cancellation-window profile, whose
  // process-operation policy deliberately rejects fork/exec admission.
  int next_fd = LIBC_NAMESPACE::fcntl(2, F_DUPFD, 3);
  ASSERT_TRUE(next_fd >= 3);
  ASSERT_EQ(LIBC_NAMESPACE::close(next_fd), 0);
  for (int i = 0; i < 32; ++i) {
    ASSERT_EQ(LIBC_NAMESPACE::popen("exit 0", i % 2 ? "we" : "r"), nullptr);
    ASSERT_EQ(libc_errno, ENOTSUP);
    int fd = LIBC_NAMESPACE::fcntl(2, F_DUPFD, 3);
    ASSERT_EQ(fd, next_fd);
    ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
  }
  return 0;
}
