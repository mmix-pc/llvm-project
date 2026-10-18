//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/stdio/tmpnam.h"
#include "test/UnitTest/Test.h"
#include <linux/stat.h>

TEST(LlvmLibcTmpnamTest, CallerBufferAndAbsentPath) {
  char first[L_tmpnam + 1], second[L_tmpnam];
  first[L_tmpnam] = '!';
  ASSERT_EQ(LIBC_NAMESPACE::tmpnam(first), first);
  ASSERT_EQ(LIBC_NAMESPACE::tmpnam(second), second);
  EXPECT_EQ(first[L_tmpnam], '!');
  bool different = false;
  size_t length = 0;
  while (length < L_tmpnam && first[length]) {
    different |= first[length] != second[length];
    ++length;
  }
  EXPECT_LT(length, size_t(L_tmpnam));
  EXPECT_TRUE(different);
  struct statx info;
  auto result = LIBC_NAMESPACE::linux_syscalls::statx(
      AT_FDCWD, first, AT_SYMLINK_NOFOLLOW, STATX_TYPE, &info);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ENOENT);
}

TEST(LlvmLibcTmpnamTest, InternalBufferIsReused) {
  char *first = LIBC_NAMESPACE::tmpnam(nullptr);
  ASSERT_NE(first, nullptr);
  char saved[L_tmpnam];
  size_t n = 0;
  do {
    saved[n] = first[n];
  } while (first[n++]);
  char *second = LIBC_NAMESPACE::tmpnam(nullptr);
  ASSERT_EQ(second, first);
  bool different = false;
  for (size_t i = 0; i < n; ++i)
    different |= second[i] != saved[i];
  EXPECT_TRUE(different);
}
