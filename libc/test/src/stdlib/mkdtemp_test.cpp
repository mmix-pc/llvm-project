//===-- Unittests for mkdtemp
//----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/types/struct_stat.h"
#include "src/stdlib/mkdtemp.h"
#include "src/string/strdup.h"
#include "src/sys/stat/stat.h"
#include "src/unistd/rmdir.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/Test.h"

using LlvmLibcMkdtempTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcMkdtempTest, UniquePrivateDirectories) {
  char *first = LIBC_NAMESPACE::strdup(libc_make_test_file_path("dir_XXXXXX"));
  char *second = LIBC_NAMESPACE::strdup(libc_make_test_file_path("dir_XXXXXX"));
  ASSERT_EQ(first, LIBC_NAMESPACE::mkdtemp(first));
  ASSERT_EQ(second, LIBC_NAMESPACE::mkdtemp(second));
  struct stat a, b;
  EXPECT_EQ(0, LIBC_NAMESPACE::stat(first, &a));
  EXPECT_EQ(0, LIBC_NAMESPACE::stat(second, &b));
  EXPECT_TRUE(S_ISDIR(a.st_mode));
  EXPECT_EQ(static_cast<mode_t>(0), a.st_mode & 0077);
  EXPECT_NE(a.st_ino, b.st_ino);
  EXPECT_EQ(0, LIBC_NAMESPACE::rmdir(first));
  EXPECT_EQ(0, LIBC_NAMESPACE::rmdir(second));
  ::free(first);
  ::free(second);
  ASSERT_ERRNO_SUCCESS();
}

TEST_F(LlvmLibcMkdtempTest, RejectsMalformedTemplate) {
  char patterns[][16] = {"", "XXXXX", "XXXXXXsuffix", "prefixXXXXXY"};
  for (auto &pattern : patterns) {
    EXPECT_EQ(static_cast<char *>(nullptr), LIBC_NAMESPACE::mkdtemp(pattern));
    ASSERT_ERRNO_EQ(EINVAL);
  }
}

TEST_F(LlvmLibcMkdtempTest, PropagatesFilesystemError) {
  char pattern[] = "/dev/null/directoryXXXXXX";
  EXPECT_EQ(static_cast<char *>(nullptr), LIBC_NAMESPACE::mkdtemp(pattern));
  ASSERT_ERRNO_EQ(ENOTDIR);
}
