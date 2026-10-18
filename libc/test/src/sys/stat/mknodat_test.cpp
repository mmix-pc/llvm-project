//===-- Unittests for mknodat ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mknodat.h"
#include "src/fcntl/open.h"
#include "src/sys/stat/fstatat.h"
#include "src/sys/stat/mknod.h"
#include "src/sys/stat/mknodat.h"
#include "src/unistd/close.h"
#include "src/unistd/getcwd.h"
#include "src/unistd/unlink.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::testing::ErrnoSetterMatcher;
using LlvmLibcMknodatTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcMknodatTest, RelativeFifo) {
  int dir = LIBC_NAMESPACE::open("testdata", O_RDONLY | O_DIRECTORY);
  ASSERT_GE(dir, 0);
  ASSERT_THAT(LIBC_NAMESPACE::mknodat(dir, "mknodat.fifo", S_IFIFO | 0600, 0),
              Succeeds(0));
  struct stat st;
  ASSERT_THAT(LIBC_NAMESPACE::fstatat(dir, "mknodat.fifo", &st, 0),
              Succeeds(0));
  EXPECT_TRUE(S_ISFIFO(st.st_mode));
  EXPECT_EQ(st.st_mode & 0077, mode_t(0));
  EXPECT_THAT(LIBC_NAMESPACE::mknodat(dir, "mknodat.fifo", S_IFIFO | 0600, 0),
              Fails(EEXIST));
  ASSERT_THAT(LIBC_NAMESPACE::unlink("testdata/mknodat.fifo"), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::close(dir), Succeeds(0));
}

TEST_F(LlvmLibcMknodatTest, AbsoluteAndCurrentDirectory) {
  char path[4096];
  ASSERT_NE(LIBC_NAMESPACE::getcwd(path, sizeof(path)), nullptr);
  size_t length = 0;
  while (path[length])
    ++length;
  constexpr char suffix[] = "/testdata/mknodat.regular";
  ASSERT_LT(length + sizeof(suffix), sizeof(path));
  for (size_t i = 0; i < sizeof(suffix); ++i)
    path[length + i] = suffix[i];
  ASSERT_THAT(LIBC_NAMESPACE::mknodat(-1, path, S_IFREG | 0600, 0),
              Succeeds(0));
  struct stat st;
  ASSERT_THAT(LIBC_NAMESPACE::fstatat(AT_FDCWD, path, &st, 0), Succeeds(0));
  EXPECT_TRUE(S_ISREG(st.st_mode));
  ASSERT_THAT(LIBC_NAMESPACE::unlink(path), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::mknodat(AT_FDCWD, "testdata/mknodat.regular",
                                      S_IFIFO | 0600, 0),
              Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::unlink(path), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::mknod("testdata/mknod.fifo", S_IFIFO | 0600, 0),
              Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::unlink("testdata/mknod.fifo"), Succeeds(0));
}

TEST_F(LlvmLibcMknodatTest, Errors) {
  EXPECT_THAT(LIBC_NAMESPACE::mknodat(-1, "relative", S_IFIFO | 0600, 0),
              Fails(EBADF));
  EXPECT_THAT(LIBC_NAMESPACE::mknodat(AT_FDCWD, "", S_IFIFO | 0600, 0),
              Fails(ENOENT));
  EXPECT_THAT(LIBC_NAMESPACE::mknodat(AT_FDCWD, "testdata/missing-dir/node",
                                      S_IFIFO | 0600, 0),
              Fails(ENOENT));
  EXPECT_THAT(LIBC_NAMESPACE::mknodat(AT_FDCWD, "testdata/mknodat.invalid",
                                      S_IFMT | 0600, 0),
              Fails(EINVAL));
  int fd = LIBC_NAMESPACE::open("testdata/mknodat.fd", O_CREAT | O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  EXPECT_THAT(LIBC_NAMESPACE::mknodat(fd, "node", S_IFIFO | 0600, 0),
              Fails(ENOTDIR));
  ASSERT_THAT(LIBC_NAMESPACE::close(fd), Succeeds(0));
  ASSERT_THAT(LIBC_NAMESPACE::unlink("testdata/mknodat.fd"), Succeeds(0));
}

TEST_F(LlvmLibcMknodatTest, DeviceEncoding) {
  // Check transport independently of CAP_MKNOD and filesystem device support.
  constexpr dev_t devices[] = {0, 0x103, 0x12345678, 0xffffffff};
  for (dev_t dev : devices) {
    auto encoded = LIBC_NAMESPACE::linux_syscalls::encode_device(dev);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, static_cast<unsigned int>(dev));
  }
  if constexpr (sizeof(dev_t) > sizeof(unsigned int)) {
    dev_t overflow = dev_t(0xffffffff) + dev_t(1);
    auto encoded = LIBC_NAMESPACE::linux_syscalls::encode_device(overflow);
    ASSERT_FALSE(encoded.has_value());
    EXPECT_EQ(encoded.error(), EINVAL);
    EXPECT_THAT(LIBC_NAMESPACE::mknodat(AT_FDCWD, "testdata/mknodat.overflow",
                                        S_IFIFO | 0600, overflow),
                Fails(EINVAL));
    EXPECT_THAT(LIBC_NAMESPACE::mknod("testdata/mknod.overflow", S_IFIFO | 0600,
                                      overflow),
                Fails(EINVAL));
  }
}
