//===-- Tests for Linux extended attributes
//--------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fcntl_macros.h"
#include "hdr/sys_xattr_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/sys/xattr/fgetxattr.h"
#include "src/sys/xattr/flistxattr.h"
#include "src/sys/xattr/fremovexattr.h"
#include "src/sys/xattr/fsetxattr.h"
#include "src/sys/xattr/getxattr.h"
#include "src/sys/xattr/lgetxattr.h"
#include "src/sys/xattr/listxattr.h"
#include "src/sys/xattr/llistxattr.h"
#include "src/sys/xattr/lremovexattr.h"
#include "src/sys/xattr/lsetxattr.h"
#include "src/sys/xattr/removexattr.h"
#include "src/sys/xattr/setxattr.h"
#include "src/unistd/close.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlink.h"
#include "test/UnitTest/Test.h"

namespace {
constexpr const char *NAME = "user.llvm_libc";
constexpr const char VALUE[] = "value";
constexpr const char *PATH = "xattr-file.test";
constexpr const char *LINK = "xattr-link.test";

class LlvmLibcXattrTest : public LIBC_NAMESPACE::testing::Test {
public:
  int fd = -1;
  void SetUp() override {
    LIBC_NAMESPACE::unlink(LINK);
    LIBC_NAMESPACE::unlink(PATH);
    fd = LIBC_NAMESPACE::open(PATH, O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_GE(fd, 0);
  }
  void TearDown() override {
    if (fd >= 0)
      LIBC_NAMESPACE::close(fd);
    LIBC_NAMESPACE::unlink(LINK);
    LIBC_NAMESPACE::unlink(PATH);
  }
};
} // namespace

TEST_F(LlvmLibcXattrTest, PathAndDescriptorRoundTrip) {
  ASSERT_EQ(
      LIBC_NAMESPACE::setxattr(PATH, NAME, VALUE, sizeof(VALUE), XATTR_CREATE),
      0);
  EXPECT_EQ(LIBC_NAMESPACE::getxattr(PATH, NAME, nullptr, 0),
            ssize_t(sizeof(VALUE)));
  char value[32]{};
  ASSERT_EQ(LIBC_NAMESPACE::fgetxattr(fd, NAME, value, sizeof(value)),
            ssize_t(sizeof(VALUE)));
  EXPECT_STREQ(value, VALUE);
  EXPECT_EQ(
      LIBC_NAMESPACE::setxattr(PATH, NAME, VALUE, sizeof(VALUE), XATTR_CREATE),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EEXIST);
  ASSERT_EQ(LIBC_NAMESPACE::fsetxattr(fd, NAME, "", 0, XATTR_REPLACE), 0);
  EXPECT_EQ(LIBC_NAMESPACE::getxattr(PATH, NAME, nullptr, 0), ssize_t(0));
  ASSERT_EQ(LIBC_NAMESPACE::fremovexattr(fd, NAME), 0);
  EXPECT_EQ(LIBC_NAMESPACE::getxattr(PATH, NAME, value, sizeof(value)),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENODATA);
  EXPECT_EQ(
      LIBC_NAMESPACE::fsetxattr(fd, NAME, VALUE, sizeof(VALUE), XATTR_REPLACE),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENODATA);
}

TEST_F(LlvmLibcXattrTest, ListsAndSmallBuffers) {
  ASSERT_EQ(LIBC_NAMESPACE::setxattr(PATH, NAME, VALUE, sizeof(VALUE), 0), 0);
  const auto size = LIBC_NAMESPACE::listxattr(PATH, nullptr, 0);
  ASSERT_GE(size, ssize_t(sizeof("user.llvm_libc")));
  char buffer[4096]{};
  ASSERT_LE(size, ssize_t(sizeof(buffer)));
  EXPECT_EQ(LIBC_NAMESPACE::flistxattr(fd, buffer, sizeof(buffer)), size);
  bool found = false;
  for (ssize_t start = 0; start < size;) {
    ssize_t end = start;
    while (end < size && buffer[end])
      ++end;
    ASSERT_LT(end, size);
    if (end - start == ssize_t(sizeof("user.llvm_libc") - 1)) {
      bool same = true;
      for (ssize_t i = start; i < end; ++i)
        same &= buffer[i] == NAME[i - start];
      found |= same;
    }
    start = end + 1;
  }
  EXPECT_TRUE(found);
  EXPECT_EQ(LIBC_NAMESPACE::listxattr(PATH, buffer, 1), ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  EXPECT_EQ(LIBC_NAMESPACE::getxattr(PATH, NAME, buffer, 1), ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
  EXPECT_EQ(LIBC_NAMESPACE::removexattr(PATH, NAME), 0);
}

TEST_F(LlvmLibcXattrTest, NoFollowOnRegularFile) {
  ASSERT_EQ(LIBC_NAMESPACE::lsetxattr(PATH, NAME, VALUE, sizeof(VALUE), 0), 0);
  char buffer[4096]{};
  EXPECT_EQ(LIBC_NAMESPACE::lgetxattr(PATH, NAME, buffer, sizeof(buffer)),
            ssize_t(sizeof(VALUE)));
  EXPECT_STREQ(buffer, VALUE);
  EXPECT_GT(LIBC_NAMESPACE::llistxattr(PATH, buffer, sizeof(buffer)),
            ssize_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::lremovexattr(PATH, NAME), 0);
}

TEST_F(LlvmLibcXattrTest, SymlinkDoesNotFollow) {
  ASSERT_EQ(LIBC_NAMESPACE::setxattr(PATH, NAME, VALUE, sizeof(VALUE), 0), 0);
  ASSERT_EQ(LIBC_NAMESPACE::symlink(PATH, LINK), 0);
  char buffer[32]{};
  EXPECT_EQ(LIBC_NAMESPACE::getxattr(LINK, NAME, buffer, sizeof(buffer)),
            ssize_t(sizeof(VALUE)));
  EXPECT_EQ(LIBC_NAMESPACE::lgetxattr(LINK, NAME, buffer, sizeof(buffer)),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENODATA);
  // Linux forbids user attributes on symlinks, even for the privileged caller.
  EXPECT_EQ(LIBC_NAMESPACE::lsetxattr(LINK, NAME, VALUE, sizeof(VALUE), 0), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EPERM);
  EXPECT_EQ(LIBC_NAMESPACE::lremovexattr(LINK, NAME), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EPERM);
  EXPECT_EQ(LIBC_NAMESPACE::getxattr(PATH, NAME, buffer, sizeof(buffer)),
            ssize_t(sizeof(VALUE)));
}

TEST_F(LlvmLibcXattrTest, ErrorsPreserveKernelResults) {
  char buffer[32];
  EXPECT_EQ(LIBC_NAMESPACE::fgetxattr(-1, NAME, buffer, sizeof(buffer)),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(LIBC_NAMESPACE::flistxattr(-1, buffer, sizeof(buffer)),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(LIBC_NAMESPACE::fremovexattr(-1, NAME), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(LIBC_NAMESPACE::fsetxattr(-1, NAME, VALUE, sizeof(VALUE), 0), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(LIBC_NAMESPACE::setxattr(PATH, NAME, VALUE, sizeof(VALUE), 4), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::getxattr("xattr-missing.test", NAME, buffer,
                                     sizeof(buffer)),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
}
