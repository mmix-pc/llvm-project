//===-- Tests for getentropy transfer and syscall errors ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/libc_errno.h"
#include "src/unistd/getentropy.h"
#include "test/UnitTest/Test.h"

namespace {
ssize_t replies[4];
unsigned count;
void *destinations[4];
size_t lengths[4];
unsigned options[4];
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {
ErrorOr<ssize_t> entropy_test_getrandom(void *buffer, size_t length,
                                      unsigned int flags) {
  // A broken retry loop must fail the test rather than loop forever.
  if (count == 4)
    return Error(EIO);
  destinations[count] = buffer;
  lengths[count] = length;
  options[count] = flags;
  ssize_t reply = replies[count++];
  if (reply < 0)
    return Error(static_cast<int>(-reply));
  for (ssize_t i = 0; i < reply; ++i)
    static_cast<unsigned char *>(buffer)[i] = static_cast<unsigned char>(count);
  return reply;
}
} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

// Replace only the transport; exercise the complete production wrapper.
#define getentropy injected_getentropy
#define getrandom entropy_test_getrandom
#include "src/unistd/linux/getentropy.cpp"
#undef getrandom
#undef getentropy

TEST(LlvmLibcGetentropySyscallTest, ShortReadsAndInterruptedRetry) {
  unsigned char bytes[10] = {};
  replies[0] = 3;
  replies[1] = -EINTR;
  replies[2] = 2;
  replies[3] = 3;
  count = 0;
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::injected_getentropy(bytes + 1, 8), 0);
  EXPECT_EQ(count, 4U);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  const size_t offsets[] = {1, 4, 4, 6}, sizes[] = {8, 5, 5, 3};
  for (unsigned i = 0; i < 4; ++i) {
    EXPECT_EQ(destinations[i], static_cast<void *>(bytes + offsets[i]));
    EXPECT_EQ(lengths[i], sizes[i]);
    EXPECT_EQ(options[i], 0U);
  }
  const unsigned char expected[] = {0, 1, 1, 1, 3, 3, 4, 4, 4, 0};
  for (unsigned i = 0; i < sizeof(bytes); ++i)
    EXPECT_EQ(bytes[i], expected[i]);
}

TEST(LlvmLibcGetentropySyscallTest, InvalidAndEmptyRequestsAvoidTransport) {
  count = 0;
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::injected_getentropy(nullptr, 0), 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::injected_getentropy(nullptr, 257), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::injected_getentropy(nullptr, size_t(-1)), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(count, 0U);
}

TEST(LlvmLibcGetentropySyscallTest, ProviderErrorsRetainWrittenPrefix) {
  const int errors[] = {ENOSYS, EFAULT, EAGAIN, EIO};
  for (int error : errors) {
    unsigned char bytes[8] = {};
    replies[0] = 2;
    replies[1] = -error;
    count = 0;
    LIBC_NAMESPACE::libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::injected_getentropy(bytes + 1, 6), -1);
    EXPECT_EQ(count, 2U);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error == ENOSYS ? ENOSYS : EIO);
    for (unsigned i = 0; i < sizeof(bytes); ++i)
      EXPECT_EQ(bytes[i], static_cast<unsigned char>(i == 1 || i == 2));
  }
}
