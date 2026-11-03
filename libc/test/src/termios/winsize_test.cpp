//===-- Linux winsize syscall-boundary tests -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/termios/tcgetwinsize.h"
#include "src/termios/tcsetwinsize.h"
#include "test/UnitTest/Test.h"
#include "test/src/termios/termios_test_syscall.h"

#include <asm/ioctls.h>
#include <sys/syscall.h>

// Compile both provider sources with termios_test_syscall.h force-included,
// as for termios_conversion_test. This does not test guest terminal behavior.
static_assert(sizeof(struct winsize) == 8);
static_assert(__builtin_offsetof(struct winsize, ws_row) == 0);
static_assert(__builtin_offsetof(struct winsize, ws_col) == 2);
static_assert(__builtin_offsetof(struct winsize, ws_xpixel) == 4);
static_assert(__builtin_offsetof(struct winsize, ws_ypixel) == 6);

static struct winsize kernel;
static unsigned long request;
static int calls, descriptor, error;
static long syscall_number;

namespace LIBC_NAMESPACE_DECL {
ErrorOr<int> termios_test_syscall(long number, int fd, unsigned long cmd,
                                unsigned long arg) {
  ++calls;
  syscall_number = number;
  descriptor = fd;
  request = cmd;
  if (cmd == TIOCGWINSZ) {
    // Deliberately write even on failure to check the provider's staging.
    *reinterpret_cast<struct winsize *>(arg) = kernel;
  } else if (cmd != TIOCSWINSZ) {
    return Error(EINVAL);
  }
  if (error)
    return Error(error);
  if (cmd == TIOCSWINSZ)
    kernel = *reinterpret_cast<const struct winsize *>(arg);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL

class LlvmLibcWinsizeTest : public LIBC_NAMESPACE::testing::Test {
  void SetUp() override {
    kernel = {24, 80, 640, 480};
    calls = descriptor = error = 0;
    request = 0;
    syscall_number = 0;
    libc_errno = 0;
  }

protected:
  void CheckSize(const struct winsize &actual, const struct winsize &expected) {
    EXPECT_EQ(actual.ws_row, expected.ws_row);
    EXPECT_EQ(actual.ws_col, expected.ws_col);
    EXPECT_EQ(actual.ws_xpixel, expected.ws_xpixel);
    EXPECT_EQ(actual.ws_ypixel, expected.ws_ypixel);
  }

  void CheckIoctl(int fd, unsigned long cmd) {
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(syscall_number, long(SYS_ioctl));
    EXPECT_EQ(descriptor, fd);
    EXPECT_EQ(request, cmd);
  }
};

TEST_F(LlvmLibcWinsizeTest, GetAllFields) {
  // These are boundary payloads, not assertions about a fresh PTY's defaults.
  const struct winsize sizes[] = {
      {0, 0, 0, 0}, {37, 113, 1920, 1080}, {65535, 32768, 65534, 32769}};
  for (const auto &size : sizes) {
    kernel = size;
    struct winsize result = {1, 2, 3, 4};
    calls = 0;
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::tcgetwinsize(19, &result), 0);
    CheckIoctl(19, TIOCGWINSZ);
    CheckSize(result, size);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST_F(LlvmLibcWinsizeTest, SetDerivedSizeAndReadBack) {
  struct winsize size;
  ASSERT_EQ(LIBC_NAMESPACE::tcgetwinsize(21, &size), 0);
  size.ws_row = 51;
  size.ws_col = 132;
  const struct winsize expected = size;
  for (int i = 0; i < 2; ++i) {
    calls = 0;
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::tcsetwinsize(21, &size), 0);
    CheckIoctl(21, TIOCSWINSZ);
    CheckSize(kernel, expected);
    CheckSize(size, expected);
    EXPECT_EQ(int(libc_errno), EDOM);

    struct winsize result = {};
    calls = 0;
    ASSERT_EQ(LIBC_NAMESPACE::tcgetwinsize(21, &result), 0);
    CheckIoctl(21, TIOCGWINSZ);
    CheckSize(result, expected);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST_F(LlvmLibcWinsizeTest, SetLinuxPixelFieldsAndBoundaryValues) {
  struct winsize size;
  ASSERT_EQ(LIBC_NAMESPACE::tcgetwinsize(23, &size), 0);
  // Pixel-field changes exercise the Linux extension, not portable POSIX input.
  const struct winsize sizes[] = {
      {65535, 32768, 65534, 32769}, {0, 0, 0, 0}};
  for (const auto &expected : sizes) {
    size = expected;
    calls = 0;
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::tcsetwinsize(23, &size), 0);
    CheckIoctl(23, TIOCSWINSZ);
    CheckSize(kernel, expected);
    CheckSize(size, expected);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST_F(LlvmLibcWinsizeTest, GetErrorsPreserveOutputAndAreNotRetried) {
  const struct winsize original = {1, 2, 3, 4};
  const int errors[] = {EBADF, ENOTTY, EINTR, EIO};
  for (int e : errors) {
    struct winsize result = original;
    error = e;
    calls = 0;
    libc_errno = 0;
    const int fd = e == EBADF ? -1 : 19;
    EXPECT_EQ(LIBC_NAMESPACE::tcgetwinsize(fd, &result), -1);
    CheckIoctl(fd, TIOCGWINSZ);
    EXPECT_EQ(int(libc_errno), e);
    CheckSize(result, original);
  }
}

TEST_F(LlvmLibcWinsizeTest, SetErrorsAreNotRetried) {
  const struct winsize original = kernel;
  struct winsize size;
  ASSERT_EQ(LIBC_NAMESPACE::tcgetwinsize(21, &size), 0);
  size.ws_row = 51;
  size.ws_col = 132;
  const struct winsize expected = size;
  const int errors[] = {EBADF, ENOTTY, EINTR, EIO, EINVAL};
  for (int e : errors) {
    error = e;
    calls = 0;
    libc_errno = 0;
    const int fd = e == EBADF ? -1 : 21;
    EXPECT_EQ(LIBC_NAMESPACE::tcsetwinsize(fd, &size), -1);
    CheckIoctl(fd, TIOCSWINSZ);
    EXPECT_EQ(int(libc_errno), e);
    CheckSize(size, expected);
    CheckSize(kernel, original);
  }
}
