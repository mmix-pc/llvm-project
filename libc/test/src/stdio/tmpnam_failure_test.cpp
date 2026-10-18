//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/tmpnam.h"
#include "test/UnitTest/Test.h"
#include <linux/stat.h>

namespace {
int random_error, stat_error, collisions, probes;
bool interrupt_random, zero_random;
} // namespace
namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {
ErrorOr<ssize_t> test_getrandom(void *data, size_t, unsigned) {
  if (interrupt_random) {
    interrupt_random = false;
    return Error(EINTR);
  }
  if (random_error)
    return Error(random_error);
  if (zero_random)
    return ssize_t(0);
  *static_cast<unsigned char *>(data) = 0x12;
  return ssize_t(1); // Exercise short reads.
}
ErrorOr<int> test_statx(int fd, const char *, int flags, unsigned mask,
                        void *) {
  EXPECT_EQ(fd, AT_FDCWD);
  EXPECT_EQ(flags, AT_SYMLINK_NOFOLLOW);
  EXPECT_EQ(mask, unsigned(STATX_TYPE));
  ++probes;
  if (probes <= collisions)
    return 0;
  return Error(stat_error ? stat_error : ENOENT);
}
} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

// Inject failures without replacing the test runner's system calls.
#define getrandom(...) test_getrandom(__VA_ARGS__)
#define statx(...) test_statx(__VA_ARGS__)
#define tmpnam injected_tmpnam
#include "src/stdio/linux/tmpnam.cpp"
#undef tmpnam
#undef statx
#undef getrandom

TEST(LlvmLibcTmpnamFailureTest, CollisionsAndShortRandomReads) {
  random_error = stat_error = probes = 0;
  collisions = 2;
  interrupt_random = true;
  zero_random = false;
  char name[L_tmpnam], other[L_tmpnam];
  EXPECT_EQ(LIBC_NAMESPACE::injected_tmpnam(name), name);
  EXPECT_EQ(probes, 3);
  EXPECT_EQ(LIBC_NAMESPACE::injected_tmpnam(other), other);
  bool different = false;
  for (size_t i = 0; name[i]; ++i)
    different |= name[i] != other[i];
  EXPECT_TRUE(different);
  EXPECT_NE(LIBC_NAMESPACE::injected_tmpnam(nullptr), nullptr);
}

TEST(LlvmLibcTmpnamFailureTest, ErrorsAndExhaustion) {
  char name[L_tmpnam] = "untouched";
  probes = collisions = stat_error = 0;
  random_error = ENOSYS;
  EXPECT_EQ(LIBC_NAMESPACE::injected_tmpnam(name), nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOSYS);
  EXPECT_EQ(probes, 0);
  EXPECT_EQ(name[0], 'u');
  random_error = 0;
  zero_random = true;
  EXPECT_EQ(LIBC_NAMESPACE::injected_tmpnam(name), nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EIO);
  zero_random = false;
  stat_error = EACCES;
  EXPECT_EQ(LIBC_NAMESPACE::injected_tmpnam(name), nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EACCES);
  stat_error = 0;
  probes = 0;
  collisions = TMP_MAX;
  EXPECT_EQ(LIBC_NAMESPACE::injected_tmpnam(name), nullptr);
  EXPECT_EQ(probes, TMP_MAX);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EEXIST);
}
