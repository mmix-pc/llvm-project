//===-- Resolver allocation failure tests --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "test/UnitTest/Test.h"

static int allocations, releases, fail_at;
static void *allocate_result(size_t size) {
  if (++allocations == fail_at)
    return nullptr;
  return ::malloc(size);
}
static void release_result(void *ptr) {
  ++releases;
  ::free(ptr);
}

#define malloc allocate_result
#include "src/netdb/getaddrinfo.cpp"
#undef malloc
#define free release_result
#include "src/netdb/freeaddrinfo.cpp"
#undef free

TEST(LlvmLibcNetdbAllocationTest, CleansPartialLists) {
  for (int failure = 1; failure <= 2; ++failure) {
    allocations = releases = 0;
    fail_at = failure;
    addrinfo *result = nullptr;
    EXPECT_EQ(LIBC_NAMESPACE::getaddrinfo("192.0.2.1", "80", nullptr, &result),
              EAI_MEMORY);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(allocations, failure);
    EXPECT_EQ(releases, failure - 1);
  }
}

TEST(LlvmLibcNetdbAllocationTest, FreesEachNodeExactlyOnce) {
  allocations = releases = fail_at = 0;
  addrinfo *result = nullptr;
  ASSERT_EQ(LIBC_NAMESPACE::getaddrinfo("192.0.2.1", "80", nullptr, &result),
            0);
  LIBC_NAMESPACE::freeaddrinfo(result);
  EXPECT_EQ(allocations, 2);
  EXPECT_EQ(releases, 2);
  LIBC_NAMESPACE::freeaddrinfo(nullptr);
  EXPECT_EQ(releases, 2);
}
