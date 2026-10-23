//===-- Tests for fgets after a recoverable read error ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/CPP/scope.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fdopen.h"
#include "src/stdio/feof.h"
#include "src/stdio/ferror.h"
#include "src/stdio/fgets.h"
#include "src/unistd/close.h"
#include "src/unistd/pipe2.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcFgetsRecoveryTest, EarlierErrorDoesNotRejectSuccessfulInput) {
  int fds[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe2(fds, O_NONBLOCK), 0);
  auto *file = LIBC_NAMESPACE::fdopen(fds[0], "r");
  LIBC_NAMESPACE::cpp::scope_exit cleanup([&] {
    if (file)
      LIBC_NAMESPACE::fclose(file);
    else
      LIBC_NAMESPACE::close(fds[0]);
    if (fds[1] >= 0)
      LIBC_NAMESPACE::close(fds[1]);
  });
  ASSERT_NE(file, nullptr);
  char buffer[16];
  EXPECT_EQ(LIBC_NAMESPACE::fgets(buffer, sizeof(buffer), file), nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EAGAIN);
  EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::feof(file), 0);

  ASSERT_EQ(LIBC_NAMESPACE::write(fds[1], "line\n", 5), ssize_t(5));
  ASSERT_EQ(LIBC_NAMESPACE::fgets(buffer, sizeof(buffer), file), buffer);
  EXPECT_STREQ(buffer, "line\n");
  EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::feof(file), 0);

  ASSERT_EQ(LIBC_NAMESPACE::write(fds[1], "part", 4), ssize_t(4));
  EXPECT_EQ(LIBC_NAMESPACE::fgets(buffer, sizeof(buffer), file), nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EAGAIN);
  EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::feof(file), 0);

  ASSERT_EQ(LIBC_NAMESPACE::write(fds[1], "tail", 4), ssize_t(4));
  ASSERT_EQ(LIBC_NAMESPACE::close(fds[1]), 0);
  fds[1] = -1;
  ASSERT_EQ(LIBC_NAMESPACE::fgets(buffer, sizeof(buffer), file), buffer);
  EXPECT_STREQ(buffer, "tail");
  EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);
  EXPECT_NE(LIBC_NAMESPACE::feof(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fgets(buffer, sizeof(buffer), file), nullptr);
}
