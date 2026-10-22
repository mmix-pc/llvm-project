//===-- Unittests for posix_madvise ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/sys_mman_macros.h"
#include "src/__support/libc_errno.h"
#include "src/sys/mman/mmap.h"
#include "src/sys/mman/munmap.h"
#include "src/sys/mman/posix_madvise.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Fails;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Succeeds;
using LlvmLibcPosixMadviseTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcPosixMadviseTest, NoError) {
  size_t alloc_size = 128;
  void *addr = LIBC_NAMESPACE::mmap(nullptr, alloc_size, PROT_READ,
                                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  ASSERT_ERRNO_SUCCESS();
  EXPECT_NE(addr, MAP_FAILED);

  EXPECT_EQ(LIBC_NAMESPACE::posix_madvise(addr, alloc_size, POSIX_MADV_RANDOM),
            0);

  int *array = reinterpret_cast<int *>(addr);
  // Reading from the memory should not crash the test.
  // Since we used the MAP_ANONYMOUS flag, the contents of the newly
  // allocated memory should be initialized to zero.
  EXPECT_EQ(array[0], 0);
  EXPECT_THAT(LIBC_NAMESPACE::munmap(addr, alloc_size), Succeeds());
}

TEST_F(LlvmLibcPosixMadviseTest, Error_BadPtr) {
  // posix_madvise is a no-op on DONTNEED, so it shouldn't fail even with the
  // nullptr.
  EXPECT_EQ(LIBC_NAMESPACE::posix_madvise(nullptr, 8, POSIX_MADV_DONTNEED), 0);

  // posix_madvise doesn't set errno, but the return value is actually the error
  // code.
#ifdef LIBC_TEST_UNDER_EMULATOR
  // QEMU might stub madvise hints, so posix_madvise might return 0.
  int ret = LIBC_NAMESPACE::posix_madvise(nullptr, 8, POSIX_MADV_SEQUENTIAL);
  EXPECT_TRUE(ret == 0 || ret == ENOMEM);
#else
  EXPECT_EQ(LIBC_NAMESPACE::posix_madvise(nullptr, 8, POSIX_MADV_SEQUENTIAL),
            ENOMEM);
#endif
}

TEST_F(LlvmLibcPosixMadviseTest, AdvicePreservesDataAndErrno) {
  constexpr size_t size = 4096;
  auto *data = static_cast<unsigned char *>(LIBC_NAMESPACE::mmap(
      nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
  ASSERT_NE(static_cast<void *>(data), MAP_FAILED);
  for (size_t i = 0; i < size; ++i)
    data[i] = static_cast<unsigned char>(i * 13 + 7);

  const int values[] = {POSIX_MADV_NORMAL, POSIX_MADV_RANDOM,
                        POSIX_MADV_SEQUENTIAL, POSIX_MADV_WILLNEED,
                        POSIX_MADV_DONTNEED};
  for (int advice : values) {
    LIBC_NAMESPACE::libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::posix_madvise(data, size, advice), 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    for (size_t i = 0; i < size; ++i)
      EXPECT_EQ(data[i], static_cast<unsigned char>(i * 13 + 7));
  }
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_THAT(LIBC_NAMESPACE::munmap(data, size), Succeeds());
}

TEST_F(LlvmLibcPosixMadviseTest, RejectsLinuxOnlyAdvice) {
  constexpr size_t size = 4096;
  // MADV_REMOVE would punch a hole in this shared anonymous mapping.
  auto *data = static_cast<unsigned char *>(LIBC_NAMESPACE::mmap(
      nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0));
  ASSERT_NE(static_cast<void *>(data), MAP_FAILED);
  for (size_t i = 0; i < size; ++i)
    data[i] = static_cast<unsigned char>(i * 13 + 7);

  const int values[] = {-1, MADV_REMOVE, MADV_FREE, MADV_DONTFORK};
  for (int advice : values) {
    LIBC_NAMESPACE::libc_errno = ERANGE;
    EXPECT_EQ(LIBC_NAMESPACE::posix_madvise(data, size, advice), EINVAL);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
    for (size_t i = 0; i < size; ++i)
      EXPECT_EQ(data[i], static_cast<unsigned char>(i * 13 + 7));
  }
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_THAT(LIBC_NAMESPACE::munmap(data, size), Succeeds());
}
