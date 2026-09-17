//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/pwd/linux/mmix/lookup.h"
#include "src/stdio/remove.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::pwd::lookup_passwd;
static bool alice(const passwd &entry) {
  return LIBC_NAMESPACE::cpp::string_view(entry.pw_name) == "alice";
}

TEST(LlvmLibcMMIXLookupTest, RecordsAndIndependentBuffers) {
  const char *path = libc_make_test_file_path("mmix-passwd.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  constexpr char records[] = "\n# comment\ninvalid\nbad:x:-1:2:x:/x:/bin/sh\n"
                             "alice:x:42:7:Alice:/home/alice:/bin/sh\n"
                             "bob:x:43:8:Bob:/home/bob:/bin/sh";
  ASSERT_EQ(file.value()->write(records, sizeof(records) - 1).value,
            sizeof(records) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  passwd entry{}, other{}, *result = nullptr;
  char buffer[128], second[128];
  libc_errno = EDOM;
  ASSERT_EQ(lookup_passwd(alice, &entry, buffer, sizeof(buffer), &result, path),
            0);
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(result, &entry);
  EXPECT_EQ(entry.pw_uid, uid_t(42));
  EXPECT_EQ(entry.pw_gid, gid_t(7));
  EXPECT_STREQ(entry.pw_dir, "/home/alice");
  auto bob = [](const passwd &p) { return p.pw_uid == 43; };
  ASSERT_EQ(lookup_passwd(bob, &other, second, sizeof(second), &result, path),
            0);
  ASSERT_EQ(result, &other);
  EXPECT_STREQ(other.pw_shell, "/bin/sh");
  EXPECT_STREQ(entry.pw_name, "alice");
  auto absent = [](const passwd &) { return false; };
  ASSERT_EQ(
      lookup_passwd(absent, &other, second, sizeof(second), &result, path), 0);
  EXPECT_EQ(result, static_cast<passwd *>(nullptr));
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcMMIXLookupTest, BoundsAndErrors) {
  const char *path = libc_make_test_file_path("mmix-passwd-bounds.test");
  constexpr char record[] = "alice:x:42:7:A:/a:/s";
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  ASSERT_EQ(file.value()->write(record, sizeof(record) - 1).value,
            sizeof(record) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  passwd entry{}, *result = &entry;
  char buffer[sizeof(record)];
  libc_errno = EDOM;
  EXPECT_EQ(lookup_passwd(alice, &entry, buffer, 0, &result, path), ERANGE);
  EXPECT_EQ(result, static_cast<passwd *>(nullptr));
  EXPECT_EQ(
      lookup_passwd(alice, &entry, buffer, sizeof(buffer) - 1, &result, path),
      ERANGE);
  EXPECT_EQ(result, static_cast<passwd *>(nullptr));
  ASSERT_EQ(lookup_passwd(alice, &entry, buffer, sizeof(buffer), &result, path),
            0);
  EXPECT_EQ(result, &entry);
  EXPECT_STREQ(entry.pw_shell, "/s");
  EXPECT_EQ(lookup_passwd(alice, &entry, buffer, sizeof(buffer), nullptr, path),
            EINVAL);
  EXPECT_EQ(
      lookup_passwd(alice, nullptr, buffer, sizeof(buffer), &result, path),
      EINVAL);
  EXPECT_EQ(
      lookup_passwd(alice, &entry, nullptr, sizeof(buffer), &result, path),
      EINVAL);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
  EXPECT_EQ(lookup_passwd(alice, &entry, buffer, sizeof(buffer), &result, path),
            ENOENT);
  EXPECT_EQ(result, static_cast<passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
}
