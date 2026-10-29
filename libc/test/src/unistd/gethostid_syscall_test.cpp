//===-- Tests for gethostid file policy and syscall errors ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
constexpr size_t MAX_READS = 12;
struct {
  unsigned char bytes[8];
  size_t length, offset;
  int fd, open_error, close_error;
  unsigned opens, reads, closes;
  bool live;
  ssize_t replies[MAX_READS];
  size_t reply_count;
  size_t requests[MAX_READS];
} state;

void reset(uint32_t value = 0x89abcdefU, size_t length = 4) {
  state = {};
  state.fd = 37;
  state.length = length;
  // Supply target-native bytes, not host-dependent hexadecimal byte strings.
  const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
  for (size_t i = 0; i < sizeof(value); ++i)
    state.bytes[i] = bytes[i];
  LIBC_NAMESPACE::libc_errno = EDOM;
}

template <size_t N> void script(const ssize_t (&replies)[N]) {
  static_assert(N <= MAX_READS);
  state.reply_count = N;
  for (size_t i = 0; i < N; ++i)
    state.replies[i] = replies[i];
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace hostid_test_syscalls {
ErrorOr<int> open(const char *path, int flags, mode_t mode) {
  EXPECT_EQ(++state.opens, 1U);
  EXPECT_FALSE(state.live);
  EXPECT_STREQ(path, "/etc/hostid");
  EXPECT_EQ(flags, O_RDONLY | O_CLOEXEC);
  EXPECT_EQ(mode, mode_t(0));
  if (state.open_error)
    return Error(state.open_error);
  state.live = true;
  return state.fd;
}

ErrorOr<ssize_t> read(int fd, void *buffer, size_t count) {
  EXPECT_EQ(fd, state.fd);
  EXPECT_TRUE(state.live);
  EXPECT_GT(count, size_t(0));
  size_t call = state.reads++;
  // Bound a broken retry loop and reject any unplanned scripted read.
  if (call >= MAX_READS || (state.reply_count && call >= state.reply_count)) {
    EXPECT_LT(call, state.reply_count ? state.reply_count : MAX_READS);
    return Error(EIO);
  }
  state.requests[call] = count;
  size_t available = state.length - state.offset;
  ssize_t reply =
      state.reply_count
          ? state.replies[call]
          : static_cast<ssize_t>(count < available ? count : available);
  if (reply < 0)
    return Error(static_cast<int>(-reply));
  size_t size = static_cast<size_t>(reply);
  EXPECT_LE(size, count);
  EXPECT_LE(size, available);
  if (size > count || size > available)
    return Error(EIO);
  for (size_t i = 0; i < size; ++i)
    static_cast<unsigned char *>(buffer)[i] = state.bytes[state.offset++];
  return reply;
}

ErrorOr<int> close(int fd) {
  EXPECT_EQ(fd, state.fd);
  EXPECT_EQ(++state.closes, 1U);
  EXPECT_TRUE(state.live);
  state.live = false;
  if (state.close_error)
    return Error(state.close_error);
  return 0;
}
} // namespace hostid_test_syscalls
} // namespace LIBC_NAMESPACE_DECL

// Exercise the real provider without touching /etc or querying the host.
#define linux_syscalls hostid_test_syscalls
#define gethostid injected_gethostid
#include "src/unistd/linux/gethostid.cpp"
#undef gethostid
#undef linux_syscalls

namespace {
void check(long expected, unsigned reads) {
  EXPECT_EQ(LIBC_NAMESPACE::injected_gethostid(), expected);
  EXPECT_EQ(state.opens, 1U);
  EXPECT_EQ(state.reads, reads);
  EXPECT_EQ(state.closes, state.open_error ? 0U : 1U);
  EXPECT_FALSE(state.live);
  // Local raw-wrapper policy only, not a portable gethostid errno contract.
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}
} // namespace

TEST(LlvmLibcGethostidSyscallTest, NativeUnsignedValuesAndEOF) {
  const uint32_t values[] = {0,           1,           0x12345678U,
                             0x80000000U, 0x89abcdefU, 0xffffffffU};
  for (uint32_t value : values) {
    reset(value);
    check(static_cast<long>(value), 2);
    EXPECT_EQ(state.offset, size_t(4));
    EXPECT_EQ(state.requests[0], size_t(4));
    EXPECT_EQ(state.requests[1], size_t(1));
  }
  reset(0x80000000U);
  state.fd = 0; // Descriptor zero is a successful open, too.
  check(static_cast<long>(0x80000000U), 2);
}

TEST(LlvmLibcGethostidSyscallTest, RejectsEveryNonFourByteLength) {
  const size_t lengths[] = {0, 1, 2, 3, 5, 6, 8};
  for (size_t length : lengths) {
    reset(0x89abcdefU, length);
    check(0, length == 0 ? 1 : 2);
    EXPECT_EQ(state.offset, length < 5 ? length : size_t(5));
  }
}

TEST(LlvmLibcGethostidSyscallTest, ShortReadsAndInterruptedDataAndEOF) {
  reset();
  const ssize_t replies[] = {-EINTR, 1, -EINTR, -EINTR, 2, 1, -EINTR, 0};
  script(replies);
  check(static_cast<long>(0x89abcdefU), 8);
  const size_t requests[] = {4, 4, 3, 3, 3, 1, 1, 1};
  for (size_t i = 0; i < 8; ++i)
    EXPECT_EQ(state.requests[i], requests[i]);
  EXPECT_EQ(state.offset, size_t(4));

  reset();
  const ssize_t single_bytes[] = {1, 1, 1, 1, 0};
  script(single_bytes);
  check(static_cast<long>(0x89abcdefU), 5);

  reset(0x89abcdefU, 3);
  const ssize_t short_file[] = {1, -EINTR, 2, -EINTR, 0};
  script(short_file);
  check(0, 5);

  reset(0x89abcdefU, 8);
  const ssize_t long_file[] = {1, 2, 1, -EINTR, 1};
  script(long_file);
  check(0, 5);
  EXPECT_EQ(state.offset, size_t(5));
}

TEST(LlvmLibcGethostidSyscallTest, OpenFailuresDoNotReadOrClose) {
  const int errors[] = {ENOENT, ENOTDIR, EACCES, EPERM, EIO, EMFILE, EINTR};
  for (int error : errors) {
    reset();
    state.open_error = error;
    check(0, 0);
  }
}

TEST(LlvmLibcGethostidSyscallTest, ReadFailuresAlwaysClose) {
  const int errors[] = {EIO, EAGAIN, EBADF, EISDIR};
  for (int error : errors) {
    for (ssize_t prefix = 0; prefix <= 4; ++prefix) {
      reset();
      if (prefix == 0) {
        const ssize_t replies[] = {-error};
        script(replies);
      } else {
        const ssize_t replies[] = {prefix, -EINTR, -error};
        script(replies);
      }
      check(0, prefix == 0 ? 1 : 3);
      EXPECT_EQ(state.offset, static_cast<size_t>(prefix));
    }
  }
}

TEST(LlvmLibcGethostidSyscallTest, CloseFailuresDiscardValueWithoutRetry) {
  const int errors[] = {EIO, EINTR, EBADF};
  for (int error : errors) {
    reset();
    state.close_error = error;
    check(0, 2);

    reset();
    state.close_error = error;
    const ssize_t replies[] = {2, -EIO};
    script(replies);
    check(0, 2);

    const size_t lengths[] = {0, 1, 3, 5, 8};
    for (size_t length : lengths) {
      reset(0x89abcdefU, length);
      state.close_error = error;
      check(0, length == 0 ? 1 : 2);
    }
  }
}
