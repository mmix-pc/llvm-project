//===-- Tests for statfs to statvfs conversion -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/statvfs/linux/statfs_utils.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcStatfsUtilsTest, MountFlags) {
  struct statfs input{};
  // Check each Linux/GNU public flag independently, including reserved bits.
  for (unsigned long flag = 1; flag <= 0x2000; flag <<= 1) {
    if (flag == 0x0020)
      continue;
    input.f_flags = flag | 0x0020;
    auto output = LIBC_NAMESPACE::statfs_utils::statfs_to_statvfs(input);
    EXPECT_EQ(output.f_flag, flag);
  }
  input.f_flags = 0x0020;
  EXPECT_EQ(LIBC_NAMESPACE::statfs_utils::statfs_to_statvfs(input).f_flag, 0UL);
  input.f_flags = 0x3fff;
  EXPECT_EQ(LIBC_NAMESPACE::statfs_utils::statfs_to_statvfs(input).f_flag,
            0x3fdfUL);
}
