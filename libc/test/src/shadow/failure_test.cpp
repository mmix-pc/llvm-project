//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/types/struct_spwd.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
int open_error;
unsigned open_count;
} // namespace

namespace LIBC_NAMESPACE_DECL {
ErrorOr<File *> shadow_test_openfile(const char *, const char *) {
  ++open_count;
  libc_errno = EBADF;
  return Error(open_error);
}
} // namespace LIBC_NAMESPACE_DECL

// Instantiate separately named copies of the production query templates so
// permission and open-allocation failures are deterministic even under root.
// The ordinary query tests exercise the actual filesystem implementation.
#define openfile shadow_test_openfile
#define lookup_account injected_lookup_account
#define lookup_shadow injected_lookup_shadow
#define getspnam_r injected_getspnam_r
#define getspnam injected_getspnam
#include "src/shadow/getspnam.cpp"
#include "src/shadow/getspnam_r.cpp"
#undef getspnam
#undef getspnam_r
#undef lookup_shadow
#undef lookup_account
#undef openfile

TEST(LlvmLibcShadowFailureTest, PermissionAndOpenAllocationErrors) {
  const int errors[] = {EACCES, EPERM, EIO, ENOMEM};
  for (int error : errors) {
    open_error = error;
    open_count = 0;
    spwd entry{}, *result = &entry;
    char buffer[128];
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::injected_getspnam_r("alice", &entry, buffer,
                                                  sizeof(buffer), &result),
              error);
    EXPECT_EQ(result, static_cast<spwd *>(nullptr));
    EXPECT_EQ(int(libc_errno), EDOM);
    EXPECT_EQ(open_count, 1U);
    EXPECT_EQ(LIBC_NAMESPACE::injected_getspnam("alice"),
              static_cast<spwd *>(nullptr));
    EXPECT_EQ(int(libc_errno), error);
    EXPECT_EQ(open_count, 2U);
  }
}

TEST(LlvmLibcShadowFailureTest, AllocationFailureAndGrowthOverflow) {
  LIBC_NAMESPACE::internal::AccountBuffer<spwd> storage;
  unsigned queries = 0, allocations = 0;
  auto range = [&queries](spwd *, char *, size_t, spwd **) {
    ++queries;
    return ERANGE;
  };
  auto failure = [&allocations](void *, size_t) -> void * {
    ++allocations;
    return nullptr;
  };
  libc_errno = EDOM;
  EXPECT_EQ(storage.lookup(range, failure), static_cast<spwd *>(nullptr));
  EXPECT_EQ(queries, 0U);
  EXPECT_EQ(allocations, 1U);
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(storage.buffer, static_cast<char *>(nullptr));
  EXPECT_EQ(storage.size, size_t(0));

  char original[16];
  storage.buffer = original;
  storage.size = sizeof(original);
  libc_errno = EDOM;
  EXPECT_EQ(storage.lookup(range, failure), static_cast<spwd *>(nullptr));
  EXPECT_EQ(queries, 1U);
  EXPECT_EQ(allocations, 2U);
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(storage.buffer, original);
  EXPECT_EQ(storage.size, sizeof(original));

  storage.size = static_cast<size_t>(-1) / 2 + 1;
  libc_errno = EDOM;
  EXPECT_EQ(storage.lookup(range, failure), static_cast<spwd *>(nullptr));
  EXPECT_EQ(queries, 2U);
  EXPECT_EQ(allocations, 2U);
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(storage.buffer, original);
}
