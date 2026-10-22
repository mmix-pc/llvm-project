//===-- Temporary-file collision and failure boundary tests ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
int random_error, open_error, collisions, calls, random_calls, seen_flags;
char first_name[7];
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace temporary_test_syscalls {
ErrorOr<ssize_t> getrandom(void *buffer, size_t length, unsigned int flags) {
  EXPECT_EQ(length, size_t(1));
  EXPECT_EQ(flags, 0U);
  ++random_calls;
  if (random_error)
    return Error(random_error);
  *static_cast<unsigned char *>(buffer) = static_cast<unsigned char>(calls);
  return ssize_t(1);
}
ErrorOr<int> open(const char *name, int flags, mode_t mode) {
  ++calls;
  seen_flags = flags;
  EXPECT_EQ(mode, mode_t(0600));
  if (calls == 1)
    for (unsigned i = 0; i < 7; ++i)
      first_name[i] = name[i];
  if (collisions-- > 0)
    return Error(EEXIST);
  if (open_error)
    return Error(open_error);
  return 17;
}
} // namespace temporary_test_syscalls
} // namespace LIBC_NAMESPACE_DECL

#define linux_syscalls temporary_test_syscalls
#include "src/stdlib/mkostemp.cpp"
#undef linux_syscalls

TEST(LlvmLibcMkostempSyscallTest, RetriesCollisionWithAtomicFlags) {
  char path[] = "XXXXXX";
  calls = random_calls = random_error = open_error = 0;
  collisions = 1;
  EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, O_CLOEXEC | O_APPEND), 17);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(random_calls, 12);
  EXPECT_EQ(seen_flags, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_APPEND);
  EXPECT_STRNE(path, first_name);
}

TEST(LlvmLibcMkostempSyscallTest, PropagatesEntropyAndOpenFailures) {
  const int errors[] = {EINTR, EIO, EMFILE, ENFILE, EACCES, ENOSPC};
  for (int error : errors) {
    char path[] = "XXXXXX";
    calls = random_calls = collisions = open_error = 0;
    random_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, 0), -1);
    EXPECT_EQ(int(libc_errno), error);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(random_calls, 1);
    EXPECT_STREQ(path, "XXXXXX");

    random_error = 0;
    open_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, 0), -1);
    EXPECT_EQ(int(libc_errno), error);
    EXPECT_EQ(calls, 1);
  }
}

TEST(LlvmLibcMkostempSyscallTest, InvalidInputsHaveNoSyscalls) {
  calls = random_calls = 0;
  char path[] = "XXXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, O_DIRECTORY), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  char bad[] = "XXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::mkostemp(bad, 0), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(random_calls, 0);
}
