//===-- Tests for Linux posix_getdents transport ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/limits.h"
#include "src/__support/File/dir.h"
#include "src/__support/libc_errno.h"
#include "src/dirent/posix_getdents.h"
#include "test/UnitTest/Test.h"

static unsigned calls;
static int observed_fd, error;
static size_t observed_size, result_size;
static void *observed_buffer;

namespace LIBC_NAMESPACE_DECL {
ErrorOr<size_t> platform_fetch_dirents(int fd, cpp::span<uint8_t> buffer) {
  ++calls;
  observed_fd = fd;
  observed_buffer = buffer.data();
  observed_size = buffer.size();
  // Synthetic transport: do not access the oversized spans in clamp checks.
  if (error)
    return Error(error);
  return result_size;
}
} // namespace LIBC_NAMESPACE_DECL

static void reset() {
  calls = 0;
  observed_fd = -1;
  error = 0;
  observed_size = result_size = 0;
  observed_buffer = nullptr;
}

TEST(LlvmLibcPosixGetdentsTransportTest, RejectsFlagsBeforeTransport) {
  const int invalid_flags[] = {1, -1};
  for (int flags : invalid_flags) {
    reset();
    EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(7, nullptr, 0, flags),
              ssize_t(-1));
    EXPECT_EQ(static_cast<int>(libc_errno), EOPNOTSUPP);
    EXPECT_EQ(calls, 0U);
  }
}

TEST(LlvmLibcPosixGetdentsTransportTest, ClampsToSignedKernelCount) {
  char buffer[32]{};
  const size_t maximum = LIBC_NAMESPACE::cpp::numeric_limits<int>::max();
  const size_t sizes[] = {0, sizeof(buffer), maximum, maximum + 1,
                          LIBC_NAMESPACE::cpp::numeric_limits<size_t>::max()};
  for (size_t size : sizes) {
    reset();
    EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(7, buffer, size, 0), ssize_t(0));
    EXPECT_EQ(calls, 1U);
    EXPECT_EQ(observed_fd, 7);
    EXPECT_EQ(observed_buffer, static_cast<void *>(buffer));
    EXPECT_EQ(observed_size, size > maximum ? maximum : size);
  }
}

TEST(LlvmLibcPosixGetdentsTransportTest, PreservesErrorsAndReturnedProgress) {
  char buffer[32]{};
  const int errors[] = {EBADF, ENOTDIR, EINVAL, EIO, EINTR};
  for (int failure : errors) {
    reset();
    error = failure;
    EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(7, buffer, sizeof(buffer), 0),
              ssize_t(-1));
    EXPECT_EQ(static_cast<int>(libc_errno), failure);
    EXPECT_EQ(calls, 1U);
    for (char byte : buffer)
      EXPECT_EQ(byte, char(0));
  }
  reset();
  result_size = 24;
  libc_errno = EBUSY;
  EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(7, buffer, sizeof(buffer), 0),
            ssize_t(24));
  EXPECT_EQ(static_cast<int>(libc_errno), EBUSY);
  EXPECT_EQ(calls, 1U);
}
