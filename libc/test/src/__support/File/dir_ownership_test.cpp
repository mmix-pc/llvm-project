//===-- Directory acquisition and retirement ownership --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/File/dir.h"
#include "hdr/errno_macros.h"
#include "test/UnitTest/Test.h"
#include <stdlib.h>

static bool fail_allocation;
static unsigned acquired, closed;
static int close_error;
static int seek_error;
extern "C" void *__real_malloc(size_t);
extern "C" void *__wrap_malloc(size_t size) {
  if (fail_allocation) {
    fail_allocation = false;
    return nullptr;
  }
  return __real_malloc(size);
}
namespace LIBC_NAMESPACE_DECL {
ErrorOr<int> platform_opendir(const char *) {
  ++acquired;
  return 7;
}
int platform_closedir(int fd) {
  EXPECT_EQ(fd, 7);
  ++closed;
  return close_error;
}
ErrorOr<size_t> platform_fetch_dirents(int, cpp::span<uint8_t>) { return 0; }
size_t platform_dir_reclen(struct dirent *) { return 0; }
off_t platform_dir_offset(const struct dirent *) { return 0; }
ErrorOr<off_t> platform_seekdir(int, off_t offset, int) {
  if (seek_error)
    return Error(seek_error);
  return offset;
}
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcDirOwnershipTest, AcquisitionFailureClosesExactlyOnce) {
  const int errors[] = {0, EIO};
  for (int error : errors) {
    acquired = closed = 0;
    close_error = error;
    fail_allocation = true;
    auto result = LIBC_NAMESPACE::Dir::open("unused");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ENOMEM);
    EXPECT_EQ(acquired, 1U);
    EXPECT_EQ(closed, 1U);
  }
}

TEST(LlvmLibcDirOwnershipTest, SuccessfulAcquisitionTransfersOwnership) {
  acquired = closed = 0;
  close_error = 0;
  auto result = LIBC_NAMESPACE::Dir::open("unused");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(acquired, 1U);
  EXPECT_EQ(closed, 0U);
  EXPECT_EQ(result.value()->close(), 0);
  EXPECT_EQ(closed, 1U);
}

TEST(LlvmLibcDirOwnershipTest, BackendMayConsumeOnCloseError) {
  acquired = closed = 0;
  close_error = EIO;
  auto result = LIBC_NAMESPACE::Dir::open("unused");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value()->close(true), EIO);
  EXPECT_EQ(closed, 1U);
}

TEST(LlvmLibcDirOwnershipTest, PositionErrorsRetainState) {
  close_error = 0;
  seek_error = 0;
  auto result = LIBC_NAMESPACE::Dir::open("unused");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value()->seek(19), 0);
  EXPECT_EQ(result.value()->tell().value(), 19L);
  seek_error = EINVAL;
  EXPECT_EQ(result.value()->seek(12), EINVAL);
  EXPECT_EQ(result.value()->tell().value(), 19L);
  EXPECT_EQ(result.value()->close(), 0);
  seek_error = 0;
}

TEST(LlvmLibcDirOwnershipTest, InitialSeekFailureClosesAcquiredDescriptor) {
  acquired = closed = 0;
  close_error = 0;
  seek_error = EIO;
  auto result = LIBC_NAMESPACE::Dir::open("unused");
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EIO);
  EXPECT_EQ(acquired, 1U);
  EXPECT_EQ(closed, 1U);
  seek_error = 0;
}
