//===-- Tests for statfs to statvfs conversion -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/stdint_proxy.h"
#include "src/sys/statvfs/linux/statfs_utils.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcStatfsUtilsTest, FilesystemIdWordsAreZeroExtended) {
  const struct {
    int low;
    int high;
    uint64_t expected;
  } cases[] = {
      {0, 0, 0x0000000000000000ULL},
      {0x7fffffff, 0, 0x000000007fffffffULL},
      {-0x7fffffff - 1, 0, 0x0000000080000000ULL},
      {-1, 0, 0x00000000ffffffffULL},
      {0, 0x12345678, 0x1234567800000000ULL},
      {0x7fffffff, 0x12345678, 0x123456787fffffffULL},
      {-0x7fffffff - 1, 0x12345678, 0x1234567880000000ULL},
      {-1, 0x12345678, 0x12345678ffffffffULL},
      {0x12345678, -0x7fffffff - 1, 0x8000000012345678ULL},
      {-1, -1, 0xffffffffffffffffULL},
  };
  for (const auto &test : cases) {
    struct statfs input{};
    input.f_fsid.__val[0] = test.low;
    input.f_fsid.__val[1] = test.high;
    auto output = LIBC_NAMESPACE::statfs_utils::statfs_to_statvfs(input);
    EXPECT_EQ(output.f_fsid,
              static_cast<decltype(output.f_fsid)>(test.expected));
  }
}

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

TEST(LlvmLibcStatfsUtilsTest, FragmentUnitsAndIndependentCounts) {
  struct statfs input{};
  input.f_bsize = 4096;
  input.f_frsize = 1024;
  input.f_blocks = 12345;
  input.f_bfree = 2345;
  input.f_bavail = 345;
  input.f_files = 6789;
  input.f_ffree = 789;
  input.f_namelen = 255;
  auto output = LIBC_NAMESPACE::statfs_utils::statfs_to_statvfs(input);
  EXPECT_EQ(output.f_bsize, 4096UL);
  EXPECT_EQ(output.f_frsize, 1024UL);
  EXPECT_EQ(output.f_blocks, static_cast<decltype(output.f_blocks)>(12345));
  EXPECT_EQ(output.f_bfree, static_cast<decltype(output.f_bfree)>(2345));
  EXPECT_EQ(output.f_bavail, static_cast<decltype(output.f_bavail)>(345));
  EXPECT_EQ(output.f_files, static_cast<decltype(output.f_files)>(6789));
  EXPECT_EQ(output.f_ffree, static_cast<decltype(output.f_ffree)>(789));
  EXPECT_EQ(output.f_favail, static_cast<decltype(output.f_favail)>(789));
  EXPECT_EQ(output.f_namemax, 255UL);
}
