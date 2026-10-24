//===-- Boundary and allocation tests for strndup -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/sys_mman_macros.h"
#include "hdr/unistd_macros.h"
#include "src/__support/libc_errno.h"
#include "src/string/strndup.h"
#include "src/sys/mman/mmap.h"
#include "src/sys/mman/mprotect.h"
#include "src/sys/mman/munmap.h"
#include "src/unistd/sysconf.h"
#include "test/UnitTest/Test.h"

namespace {
bool fail_allocation = false;
size_t allocation_size = 0;
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void *__wrap_malloc(size_t size) {
  if (fail_allocation) {
    allocation_size = size;
    return nullptr;
  }
  return __real_malloc(size);
}

TEST(LlvmLibcStrndupBoundaryTest, UnterminatedInputAtPageBoundary) {
  long page = LIBC_NAMESPACE::sysconf(_SC_PAGESIZE);
  ASSERT_GT(page, 0L);
  size_t size = static_cast<size_t>(page);
  void *mapping =
      LIBC_NAMESPACE::mmap(nullptr, 2 * size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(mapping, MAP_FAILED);
  char *bytes = static_cast<char *>(mapping);
  int result = LIBC_NAMESPACE::mprotect(bytes + size, size, PROT_NONE);
  if (result != 0) {
    LIBC_NAMESPACE::munmap(mapping, 2 * size);
    ASSERT_EQ(result, 0);
    return;
  }
  for (size_t count = 1; count <= 128; ++count) {
    char *input = bytes + size - count;
    for (size_t i = 0; i != count; ++i)
      input[i] = char('A' + i % 26);
    char *copy = LIBC_NAMESPACE::strndup(input, count);
    EXPECT_NE(copy, nullptr);
    if (copy) {
      for (size_t i = 0; i != count; ++i)
        EXPECT_EQ(copy[i], input[i]);
      EXPECT_EQ(copy[count], '\0');
      ::free(copy);
    }
  }
  // A zero bound must not inspect even the first source byte.
  char *empty = LIBC_NAMESPACE::strndup(bytes + size, 0);
  EXPECT_NE(empty, nullptr);
  if (empty) {
    EXPECT_STREQ(empty, "");
    ::free(empty);
  }
  EXPECT_EQ(LIBC_NAMESPACE::munmap(mapping, 2 * size), 0);
}

TEST(LlvmLibcStrndupBoundaryTest, AllocationFailure) {
  const char input[] = {'a', 'b', 'c'};
  for (size_t count = 0; count <= sizeof(input); ++count) {
    libc_errno = EBUSY;
    allocation_size = 0;
    fail_allocation = true;
    char *copy = LIBC_NAMESPACE::strndup(input, count);
    fail_allocation = false;
    EXPECT_EQ(copy, nullptr);
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_EQ(allocation_size, count + 1);
  }
}
