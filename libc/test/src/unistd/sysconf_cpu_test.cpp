//===-- Tests for Linux sysconf configured CPU counts ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/unistd_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/libc_errno.h"
#include "src/unistd/sysconf.h"
#include "sysconf_cpu_test_syscall.h"
#include "test/UnitTest/Test.h"

namespace {
using LIBC_NAMESPACE::cpp::string_view;
bool injecting;
string_view contents;
size_t offset, chunk_size = 128;
int open_error, read_error, close_error;
int open_interrupts, read_interrupts;
int opens, closes, affinities;
unsigned char affinity_mask = 1;
bool publish_on_close;
long nested_result;
constexpr int TEST_FD = 1000000;

void expect_failure(string_view input, int expected_error) {
  contents = input;
  int old_opens = opens;
  int old_closes = closes;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_CONF), -1L);
  EXPECT_EQ(int(libc_errno), expected_error);
  EXPECT_GT(opens, old_opens);
  EXPECT_EQ(closes - old_closes, open_error ? 0 : 1);
  EXPECT_EQ(affinities, 0);
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
int sysconf_cpu_test_open(long number, int dirfd, const char *path, int flags,
                          mode_t mode) {
  if (!injecting ||
      (string_view(path) != "/sys/devices/system/cpu/possible" &&
       string_view(path) != "/sys/devices/system/cpu/online")) {
#ifdef SYS_openat
    return syscall_impl(number, long(dirfd), reinterpret_cast<long>(path),
                        long(flags), long(mode));
#else
    return syscall_impl(number, reinterpret_cast<long>(path), long(flags),
                        long(mode));
#endif
  }
  ++opens;
  EXPECT_EQ(dirfd, AT_FDCWD);
  EXPECT_EQ(flags, O_RDONLY | O_CLOEXEC);
  if (open_interrupts > 0) {
    --open_interrupts;
    return -EINTR;
  }
  offset = 0;
  return open_error ? -open_error : TEST_FD;
}

ssize_t sysconf_cpu_test_read(long number, int fd, void *buffer, size_t size) {
  if (!injecting || fd != TEST_FD)
    return syscall_impl(number, long(fd), reinterpret_cast<long>(buffer),
                        long(size));
  EXPECT_EQ(number, long(SYS_read));
  if (read_interrupts > 0) {
    --read_interrupts;
    return -EINTR;
  }
  if (offset == contents.size())
    return -read_error;
  size_t count = contents.size() - offset;
  if (count > size)
    count = size;
  if (count > chunk_size)
    count = chunk_size;
  __builtin_memcpy(buffer, contents.data() + offset, count);
  offset += count;
  return static_cast<ssize_t>(count);
}

int sysconf_cpu_test_close(long number, int fd) {
  if (!injecting || fd != TEST_FD)
    return syscall_impl(number, long(fd));
  EXPECT_EQ(number, long(SYS_close));
  ++closes;
  if (publish_on_close) {
    // Deterministically interleave a competing reader after this reader has
    // parsed its value but before it can publish. No scheduler timing needed.
    publish_on_close = false;
    contents = "0-5\n";
    nested_result = LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_CONF);
  }
  return -close_error;
}

int sysconf_cpu_test_affinity(long number, pid_t tid, size_t size,
                              unsigned char *mask) {
  if (!injecting)
    return syscall_impl(number, long(tid), long(size),
                        reinterpret_cast<long>(mask));
  EXPECT_EQ(number, long(SYS_sched_getaffinity));
  ++affinities;
  for (size_t i = 0; i < size; ++i)
    mask[i] = i == 0 ? affinity_mask : 0;
  return static_cast<int>(size);
}
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcSysconfCpuTest, ErrorsRetryAndFirstSuccessfulPublicationWins) {
  injecting = true;
  // No successful configured query precedes these failures in this process.
  // A changing affinity must never manufacture a configured count.
  open_error = ENOENT;
  affinity_mask = 3;
  expect_failure("", ENOENT);
  affinity_mask = 1;
  expect_failure("", ENOENT);
  open_error = EACCES;
  expect_failure("", EACCES);
  open_error = 0;

  const string_view malformed[] = {"",       "\n",    ",0",   "0,",
                                   "0,,2",  "0-",    "2-1",  "0,0",
                                   "0-2,2", "2,1",   "0 1",  "0\n1",
                                   "0\n\n", "0-x",   "0-1x", "0\r\n"};
  chunk_size = 1;
  for (string_view input : malformed)
    expect_failure(input, EIO);
  expect_failure("184467440737095516160", EOVERFLOW);
  expect_failure("0-9223372036854775807", EOVERFLOW);
  expect_failure("0-9223372036854775806,9223372036854775807", EOVERFLOW);

  read_error = EIO;
  close_error = EBADF;
  expect_failure("0-7\n", EIO);
  read_error = 0;
  close_error = 0;

  // Online counts are deliberately not cached and cannot seed the configured
  // cache. This remains true when the online query uses its legacy fallback.
  contents = "0-1\n";
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_ONLN), 2L);
  contents = "0\n";
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_ONLN), 1L);
  EXPECT_EQ(int(libc_errno), EDOM);
  open_error = ENOENT;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_ONLN), 1L);
  EXPECT_EQ(affinities, 1);
  affinities = 0;
  expect_failure("", ENOENT);
  open_error = 0;

  // Cross the parser's actual 128-byte boundary in a number. The outer
  // reader sees eight CPUs; the nested reader publishes six first.
  char split_number[140];
  for (size_t i = 0; i < sizeof(split_number) - 4; ++i)
    split_number[i] = '0';
  split_number[136] = '0';
  split_number[137] = '-';
  split_number[138] = '7';
  split_number[139] = '\n';
  contents = string_view(split_number, sizeof(split_number));
  chunk_size = 128;
  open_interrupts = read_interrupts = 1;
  close_error = EINTR;
  publish_on_close = true;
  int before_publication_closes = closes;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_CONF), 6L);
  EXPECT_EQ(nested_result, 6L);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(open_interrupts, 0);
  EXPECT_EQ(read_interrupts, 0);
  EXPECT_EQ(closes - before_publication_closes, 2);

  int old_opens = opens;
  int old_closes = closes;
  open_error = ENOENT;
  contents = "0\n";
  for (int i = 0; i < 3; ++i) {
    libc_errno = i == 0 ? 0 : ERANGE;
    EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_CONF), 6L);
    EXPECT_EQ(int(libc_errno), i == 0 ? 0 : ERANGE);
  }
  EXPECT_EQ(opens, old_opens);
  EXPECT_EQ(closes, old_closes);
  EXPECT_EQ(affinities, 0);
  injecting = false;
}
