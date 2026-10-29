//===-- Login name wrapper injection tests --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/types/size_t.h"
#include "hdr/utmp_macros.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
struct {
  unsigned tty_calls;
  int fd;
  size_t terminal_size;
  const char *terminal;
  int tty_error;
  unsigned login_calls;
  char line[PATH_MAX];
  char *buffer;
  size_t size;
  const char *user;
  int login_error;
} state;

void reset(int saved_errno = EDOM) {
  state = {};
  state.fd = -1;
  state.terminal = "/dev/pts/42";
  state.user = "alice";
  LIBC_NAMESPACE::libc_errno = saved_errno;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
int getlogin_test_ttyname_r(int fd, char *buffer, size_t size) {
  ++state.tty_calls;
  state.fd = fd;
  state.terminal_size = size;
  libc_errno = EIO;
  if (state.tty_error)
    return state.tty_error;
  for (size_t i = 0; i < size; ++i) {
    buffer[i] = state.terminal[i];
    if (!buffer[i])
      return 0;
  }
  return ERANGE;
}

namespace utmp_backend {
int getlogin_test_login_name(const char *line, char *buffer, size_t size) {
  ++state.login_calls;
  // The wrapper's terminal buffer is only live during this call.
  size_t i = 0;
  for (; i < sizeof(state.line) - 1 && line[i]; ++i)
    state.line[i] = line[i];
  state.line[i] = '\0';
  state.buffer = buffer;
  state.size = size;
  libc_errno = EACCES;
  if (state.login_error)
    return state.login_error;
  size_t used = 0;
  while (state.user[used])
    ++used;
  if (size <= used)
    return ERANGE;
  if (!buffer)
    return EINVAL;
  for (i = 0; i <= used; ++i)
    buffer[i] = state.user[i];
  return 0;
}
} // namespace utmp_backend
} // namespace LIBC_NAMESPACE_DECL

// Compile both production wrappers with private entrypoints. Neither terminal
// discovery nor accounting lookup can access the host's session information.
#define ttyname_r getlogin_test_ttyname_r
#define login_name getlogin_test_login_name
#define getlogin_r injected_getlogin_r
#define getlogin injected_getlogin
#include "src/unistd/linux/getlogin_r.cpp"
#include "src/unistd/linux/getlogin.cpp"
#undef getlogin
#undef getlogin_r
#undef login_name
#undef ttyname_r

TEST(LlvmLibcGetloginTest, ReentrantForwardsTerminalAndBufferPreservingErrno) {
  const struct {
    const char *terminal;
    const char *line;
  } paths[] = {{"/dev/pts/42", "pts/42"},
               {"/dev/ttyS0", "ttyS0"},
               {"/dev/serial/dev/tty0", "serial/dev/tty0"}};
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    for (const auto &path : paths) {
      reset(saved_errno);
      state.terminal = path.terminal;
      char name[UT_NAMESIZE + 3];
      for (char &c : name)
        c = '!';
      EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin_r(name + 1, sizeof(name) - 2),
                0);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
      EXPECT_EQ(state.tty_calls, 1U);
      EXPECT_EQ(state.fd, 0);
      EXPECT_EQ(state.terminal_size, size_t(PATH_MAX));
      EXPECT_EQ(state.login_calls, 1U);
      EXPECT_STREQ(state.line, path.line);
      EXPECT_EQ(state.buffer, name + 1);
      EXPECT_EQ(state.size, sizeof(name) - 2);
      EXPECT_STREQ(name + 1, "alice");
      EXPECT_EQ(name[0], '!');
      EXPECT_EQ(name[7], '!');
      EXPECT_EQ(name[sizeof(name) - 1], '!');
    }
  }
}

TEST(LlvmLibcGetloginTest, ReentrantTerminalErrorsPreserveErrno) {
  const int errors[] = {EBADF, ENOTTY, ERANGE, EINTR, EIO};
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    for (int error : errors) {
      reset(saved_errno);
      state.tty_error = error;
      char name[] = "unchanged";
      EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin_r(name, sizeof(name)), error);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
      EXPECT_EQ(state.tty_calls, 1U);
      EXPECT_EQ(state.fd, 0);
      EXPECT_EQ(state.login_calls, 0U);
      EXPECT_STREQ(name, "unchanged");
    }
  }
}

TEST(LlvmLibcGetloginTest, ReentrantRejectsNonDevicePathsPreservingErrno) {
  const char *paths[] = {"",          "/",           "/dev",
                         "/devx/tty0", "dev/pts/42", "/tmp/pts/42"};
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    for (const char *path : paths) {
      reset(saved_errno);
      state.terminal = path;
      char name[] = "unchanged";
      EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin_r(name, sizeof(name)), ENOENT);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
      EXPECT_EQ(state.tty_calls, 1U);
      EXPECT_EQ(state.login_calls, 0U);
      EXPECT_STREQ(name, "unchanged");
    }
  }
}

TEST(LlvmLibcGetloginTest, ReentrantLookupErrorsPreserveErrno) {
  const int errors[] = {ENOENT, ERANGE, EINVAL, EIO, EACCES, EAGAIN};
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    for (int error : errors) {
      reset(saved_errno);
      state.login_error = error;
      char name[] = "unchanged";
      EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin_r(name, sizeof(name)), error);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
      EXPECT_EQ(state.tty_calls, 1U);
      EXPECT_EQ(state.login_calls, 1U);
      EXPECT_STREQ(state.line, "pts/42");
      EXPECT_EQ(state.buffer, name);
      EXPECT_EQ(state.size, sizeof(name));
      EXPECT_STREQ(name, "unchanged");
    }
  }
}

TEST(LlvmLibcGetloginTest, ReentrantForwardsEmptyLineAndOutputArguments) {
  reset();
  state.terminal = "/dev/";
  state.login_error = ENOENT;
  char name[] = "unchanged";
  EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin_r(name, sizeof(name)), ENOENT);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(state.login_calls, 1U);
  EXPECT_STREQ(state.line, "");
  EXPECT_STREQ(name, "unchanged");

  reset();
  EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin_r(name, 0), ERANGE);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(state.tty_calls, 1U);
  EXPECT_EQ(state.login_calls, 1U);
  EXPECT_EQ(state.buffer, name);
  EXPECT_EQ(state.size, size_t(0));
  EXPECT_STREQ(name, "unchanged");

  reset();
  EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin_r(nullptr, 33), EINVAL);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(state.tty_calls, 1U);
  EXPECT_EQ(state.login_calls, 1U);
  EXPECT_EQ(state.buffer, nullptr);
  EXPECT_EQ(state.size, size_t(33));
}

TEST(LlvmLibcGetloginTest, BorrowedBufferIsReusedAndHoldsFullUsername) {
  reset();
  state.user = "abcdefghijklmnopqrstuvwxyz012345";
  char *first = LIBC_NAMESPACE::injected_getlogin();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(state.tty_calls, 1U);
  EXPECT_EQ(state.fd, 0);
  EXPECT_EQ(state.login_calls, 1U);
  EXPECT_EQ(state.buffer, first);
  EXPECT_EQ(state.size, size_t(33));
  EXPECT_STREQ(first, state.user);
  EXPECT_EQ(first[32], '\0');

  reset(0);
  state.user = "bob";
  char *second = LIBC_NAMESPACE::injected_getlogin();
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), 0);
  EXPECT_EQ(second, first);
  EXPECT_EQ(state.buffer, second);
  EXPECT_EQ(state.size, size_t(UT_NAMESIZE + 1));
  EXPECT_STREQ(first, "bob");
}

TEST(LlvmLibcGetloginTest, BorrowedBufferFailureReturnsNullAndSetsErrno) {
  reset();
  char *borrowed = LIBC_NAMESPACE::injected_getlogin();
  ASSERT_NE(borrowed, nullptr);
  const int terminal_errors[] = {EBADF, ENOTTY, EINTR};
  for (int error : terminal_errors) {
    reset();
    state.tty_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin(), static_cast<char *>(nullptr));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.tty_calls, 1U);
    EXPECT_EQ(state.login_calls, 0U);
  }
  const int lookup_errors[] = {ENOENT, ERANGE, EIO, EACCES};
  for (int error : lookup_errors) {
    reset();
    state.login_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::injected_getlogin(), static_cast<char *>(nullptr));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.login_calls, 1U);
    EXPECT_EQ(state.buffer, borrowed);
    EXPECT_EQ(state.size, size_t(UT_NAMESIZE + 1));
  }
  reset();
  char *recovered = LIBC_NAMESPACE::injected_getlogin();
  ASSERT_NE(recovered, nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(recovered, borrowed);
  EXPECT_STREQ(recovered, "alice");
}
