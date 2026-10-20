//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/shadow/getspnam.h"
#include "src/shadow/getspnam_r.h"
#include "src/shadow/lookup.h"
#include "src/stdio/remove.h"
#include "src/unistd/close.h"
#include "src/unistd/dup.h"
#include "test/UnitTest/Test.h"

namespace {
struct LookupPath {
  explicit LookupPath(const char *path) {
    LIBC_NAMESPACE::shadow_db::TESTONLY_set_lookup_path(path);
  }
  ~LookupPath() {
    LIBC_NAMESPACE::shadow_db::TESTONLY_set_lookup_path(nullptr);
  }
};
} // namespace

TEST(LlvmLibcShadowQueryTest, RecordsAndIndependentBuffers) {
  auto path = libc_make_test_file_path("shadow-query.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  // Malformed records deliberately have the requested name.
  const char records[] = "\n# comment\ninvalid\n:x:1:2:3:4:5:6:7\n"
                         "alice:x:1:2:3:4:5:6\nalice:x:1:2:3:4:5:6:7:extra\n"
                         "alice:x:999999999999999999999999999999:2:3:4:5:6:7\n"
                         "alice:x:1:2:3:4:5:6:999999999999999999999999999999\n"
                         "alice:x:1:2:3:4:5x:6:7\nalice:x:1:2:3:4:5:6:7\0junk\n"
                         "alice:$6$salt$hash:20000:0:90:7:30:22000:42\n"
                         "empty::::::::\nbob:!:3:4:5:6:7:8:9";
  ASSERT_EQ(file.value()->write(records, sizeof(records) - 1).value,
            sizeof(records) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  LookupPath override(path);
  spwd entry{}, other{}, *result = nullptr;
  char buffer[256], second[256];
  libc_errno = EDOM;
  ASSERT_EQ(LIBC_NAMESPACE::getspnam_r("alice", &entry, buffer, sizeof(buffer),
                                       &result),
            0);
  ASSERT_EQ(result, &entry);
  EXPECT_EQ(entry.sp_namp, buffer);
  EXPECT_EQ(entry.sp_pwdp, buffer + 6);
  EXPECT_STREQ(entry.sp_pwdp, "$6$salt$hash");
  EXPECT_EQ(entry.sp_lstchg, 20000L);
  EXPECT_EQ(entry.sp_flag, 42UL);
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(LIBC_NAMESPACE::getspnam_r("bob", &other, second, sizeof(second),
                                       &result),
            0);
  ASSERT_EQ(result, &other);
  EXPECT_STREQ(other.sp_pwdp, "!");
  EXPECT_EQ(other.sp_expire, 8L);
  EXPECT_STREQ(entry.sp_namp, "alice");
  EXPECT_STREQ(entry.sp_pwdp, "$6$salt$hash");
  ASSERT_EQ(LIBC_NAMESPACE::getspnam_r("empty", &other, second, sizeof(second),
                                       &result),
            0);
  ASSERT_EQ(result, &other);
  EXPECT_EQ(other.sp_lstchg, -1L);
  EXPECT_EQ(other.sp_flag, static_cast<unsigned long>(-1));
  const char *missing_names[] = {"missing", "alic", ""};
  for (const char *missing : missing_names) {
    result = &other;
    EXPECT_EQ(LIBC_NAMESPACE::getspnam_r(missing, &other, second,
                                         sizeof(second), &result),
              0);
    EXPECT_EQ(result, static_cast<spwd *>(nullptr));
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  auto *shared = LIBC_NAMESPACE::getspnam("alice");
  ASSERT_NE(shared, nullptr);
  EXPECT_STREQ(shared->sp_namp, "alice");
  EXPECT_EQ(LIBC_NAMESPACE::getspnam("bob"), shared);
  EXPECT_STREQ(shared->sp_namp, "bob");
  EXPECT_STREQ(entry.sp_pwdp, "$6$salt$hash");
  EXPECT_EQ(LIBC_NAMESPACE::getspnam("missing"), static_cast<spwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcShadowQueryTest, ExactCapacityAndErrors) {
  auto path = libc_make_test_file_path("shadow-bounds.test");
  const char record[] = "u:x:1:2:3:4:5:6:7";
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  ASSERT_EQ(file.value()->write(record, sizeof(record) - 1).value,
            sizeof(record) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  LookupPath override(path);
  spwd entry{}, *result = &entry;
  char buffer[sizeof(record) + 1];
  libc_errno = EDOM;
  const size_t sizes[] = {0, 1, sizeof(record) - 1};
  for (size_t size : sizes) {
    buffer[size] = 'Z';
    EXPECT_EQ(LIBC_NAMESPACE::getspnam_r("u", &entry, buffer, size, &result),
              ERANGE);
    EXPECT_EQ(buffer[size], 'Z');
    EXPECT_EQ(result, static_cast<spwd *>(nullptr));
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  buffer[sizeof(record)] = 'Z';
  ASSERT_EQ(
      LIBC_NAMESPACE::getspnam_r("u", &entry, buffer, sizeof(record), &result),
      0);
  EXPECT_EQ(result, &entry);
  EXPECT_EQ(buffer[sizeof(record)], 'Z');
  EXPECT_EQ(LIBC_NAMESPACE::getspnam_r(nullptr, &entry, buffer, sizeof(buffer),
                                       &result),
            EINVAL);
  EXPECT_EQ(result, static_cast<spwd *>(nullptr));
  EXPECT_EQ(
      LIBC_NAMESPACE::getspnam_r("u", nullptr, buffer, sizeof(buffer), &result),
      EINVAL);
  EXPECT_EQ(
      LIBC_NAMESPACE::getspnam_r("u", &entry, nullptr, sizeof(buffer), &result),
      EINVAL);
  EXPECT_EQ(
      LIBC_NAMESPACE::getspnam_r("u", &entry, buffer, sizeof(buffer), nullptr),
      EINVAL);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::getspnam(nullptr), static_cast<spwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EINVAL);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
  libc_errno = EDOM;
  result = &entry;
  EXPECT_EQ(
      LIBC_NAMESPACE::getspnam_r("u", &entry, buffer, sizeof(buffer), &result),
      ENOENT);
  EXPECT_EQ(result, static_cast<spwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::getspnam("u"), static_cast<spwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOENT);
}

TEST(LlvmLibcShadowQueryTest, LargePasswordAndGrowth) {
  auto path = libc_make_test_file_path("shadow-growth.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  ASSERT_EQ(file.value()->write("alice:", 6).value, size_t(6));
  char password[4096];
  for (char &ch : password)
    ch = 'A';
  ASSERT_EQ(file.value()->write(password, sizeof(password)).value,
            sizeof(password));
  const char tail[] = ":1:2:3:4:5:6:7\n";
  ASSERT_EQ(file.value()->write(tail, sizeof(tail) - 1).value,
            sizeof(tail) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  LookupPath override(path);
  libc_errno = EDOM;
  auto *entry = LIBC_NAMESPACE::getspnam("alice");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->sp_pwdp[4095], 'A');
  EXPECT_EQ(entry->sp_pwdp[4096], '\0');
  EXPECT_EQ(entry->sp_flag, 7UL);
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcShadowQueryTest, ReadErrorClosesFile) {
  LookupPath override(".");
  int before = LIBC_NAMESPACE::dup(1);
  ASSERT_GE(before, 0);
  ASSERT_EQ(LIBC_NAMESPACE::close(before), 0);
  spwd entry{}, *result = &entry;
  char buffer[128];
  libc_errno = EDOM;
  for (unsigned i = 0; i < 64; ++i) {
    EXPECT_EQ(LIBC_NAMESPACE::getspnam_r("u", &entry, buffer, sizeof(buffer),
                                         &result),
              EISDIR);
    EXPECT_EQ(result, static_cast<spwd *>(nullptr));
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  int after = LIBC_NAMESPACE::dup(1);
  ASSERT_GE(after, 0);
  EXPECT_EQ(after, before);
  ASSERT_EQ(LIBC_NAMESPACE::close(after), 0);
}
