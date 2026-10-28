//===-- Tests for reallocarray --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/reallocarray.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
bool intercept = false;
unsigned calls;
void *input;
size_t bytes;
void *output;
int result_errno;
} // namespace

extern "C" void *__real_realloc(void *, size_t);
extern "C" void *__wrap_realloc(void *ptr, size_t size) {
  if (!intercept)
    return __real_realloc(ptr, size);
  ++calls;
  input = ptr;
  bytes = size;
  if (result_errno)
    libc_errno = result_errno;
  return output;
}

TEST(LlvmLibcReallocarrayTest, OverflowDoesNotCallAllocator) {
  unsigned char original[] = {1, 2, 3, 4};
  calls = 0;
  libc_errno = EDOM;
  intercept = true;
  void *result = LIBC_NAMESPACE::reallocarray(original, size_t(-1), 2);
  intercept = false;
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(calls, 0U);
  EXPECT_EQ(int(libc_errno), ENOMEM);
  for (unsigned i = 0; i < sizeof(original); ++i)
    EXPECT_EQ(original[i], static_cast<unsigned char>(i + 1));
}

TEST(LlvmLibcReallocarrayTest, DelegatesSizeOwnershipAndErrors) {
  unsigned char original[8] = {}, replacement[16] = {};
  struct Case {
    size_t count;
    size_t size;
    int error;
  };
  const Case cases[] = {{2, 4, 0}, {4, 2, ENOMEM}, {0, size_t(-1), EINVAL},
                        {size_t(-1), 0, EINVAL}, {1, size_t(-1), ENOMEM}};
  for (const Case &c : cases) {
    calls = 0;
    output = c.error ? nullptr : replacement;
    result_errno = c.error;
    libc_errno = EDOM;
    intercept = true;
    void *result = LIBC_NAMESPACE::reallocarray(original, c.count, c.size);
    intercept = false;
    EXPECT_EQ(result, output);
    EXPECT_EQ(input, static_cast<void *>(original));
    EXPECT_EQ(bytes, c.count * c.size);
    EXPECT_EQ(calls, 1U);
    EXPECT_EQ(int(libc_errno), c.error ? c.error : EDOM);
  }
  calls = 0;
  result_errno = 0;
  output = nullptr;
  libc_errno = EDOM;
  intercept = true;
  void *result = LIBC_NAMESPACE::reallocarray(nullptr, 0, 0);
  intercept = false;
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(input, nullptr);
  EXPECT_EQ(bytes, size_t(0));
  EXPECT_EQ(calls, 1U);
  EXPECT_EQ(int(libc_errno), EDOM);
}
