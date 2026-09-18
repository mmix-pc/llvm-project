//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/pwd/getpwnam.h"
#include "src/pwd/getpwnam_r.h"
#include "src/pwd/getpwuid.h"
#include "src/pwd/getpwuid_r.h"
#include "src/pwd/lookup.h"
#include "src/stdio/remove.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcPwdQueryTest, PublicQueriesAndGrowth) {
  const char *path = libc_make_test_file_path("passwd-query.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  const char start[] = "alice:x:42:7:";
  ASSERT_EQ(file.value()->write(start, sizeof(start) - 1).value,
            sizeof(start) - 1);
  char long_gecos[4096];
  for (char &ch : long_gecos)
    ch = 'A';
  ASSERT_EQ(file.value()->write(long_gecos, sizeof(long_gecos)).value,
            sizeof(long_gecos));
  const char rest[] = ":/home/alice:/bin/sh\nbob:x:43:8:B:/b:/s\n";
  ASSERT_EQ(file.value()->write(rest, sizeof(rest) - 1).value,
            sizeof(rest) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  LIBC_NAMESPACE::pwd::TESTONLY_set_lookup_path(path);
  libc_errno = EDOM;
  auto *entry = LIBC_NAMESPACE::getpwnam("alice");
  ASSERT_TRUE(entry != nullptr);
  EXPECT_EQ(entry->pw_uid, uid_t(42));
  EXPECT_EQ(entry->pw_gecos[4095], 'A');
  EXPECT_EQ(entry->pw_gecos[4096], '\0');
  EXPECT_EQ(int(libc_errno), EDOM);
  auto *other = LIBC_NAMESPACE::getpwuid(43);
  ASSERT_TRUE(other != nullptr);
  EXPECT_EQ(entry, other);
  EXPECT_STREQ(other->pw_name, "bob");
  EXPECT_EQ(LIBC_NAMESPACE::getpwuid(99), static_cast<passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  passwd pw{}, *result = &pw;
  char buffer[16];
  EXPECT_EQ(
      LIBC_NAMESPACE::getpwnam_r("alice", &pw, buffer, sizeof(buffer), &result),
      ERANGE);
  EXPECT_EQ(result, static_cast<passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::getpwnam(nullptr), static_cast<passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EINVAL);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
  EXPECT_EQ(LIBC_NAMESPACE::getpwuid(42), static_cast<passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOENT);
  LIBC_NAMESPACE::pwd::TESTONLY_set_lookup_path(nullptr);
}

TEST(LlvmLibcPwdQueryTest, AllocationFailureAndOverflow) {
  LIBC_NAMESPACE::internal::AccountBuffer<passwd> storage;
  auto range = [](passwd *, char *, size_t, passwd **) { return ERANGE; };
  unsigned allocations = 0;
  auto failure = [&allocations](void *, size_t) -> void * {
    ++allocations;
    return nullptr;
  };
  EXPECT_EQ(storage.lookup(range, failure), static_cast<passwd *>(nullptr));
  EXPECT_EQ(allocations, 1u);
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(storage.buffer, static_cast<char *>(nullptr));
  char original[16];
  storage.buffer = original;
  storage.size = sizeof(original);
  EXPECT_EQ(storage.lookup(range, failure), static_cast<passwd *>(nullptr));
  EXPECT_EQ(storage.buffer, original);
  EXPECT_EQ(storage.size, sizeof(original));
  EXPECT_EQ(allocations, 2u);
  storage.size = static_cast<size_t>(-1) / 2 + 1;
  EXPECT_EQ(storage.lookup(range, failure), static_cast<passwd *>(nullptr));
  EXPECT_EQ(allocations, 2u);
  EXPECT_EQ(int(libc_errno), ENOMEM);
}
