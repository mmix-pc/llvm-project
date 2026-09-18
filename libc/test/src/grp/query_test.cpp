//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/stdint_proxy.h"
#include "src/grp/getgrgid.h"
#include "src/grp/getgrgid_r.h"
#include "src/grp/getgrnam.h"
#include "src/grp/getgrnam_r.h"
#include "src/grp/lookup.h"
#include "src/stdio/remove.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcGroupQueryTest, RecordsBuffersAndLifetime) {
  const char *path = libc_make_test_file_path("group-query.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  const char records[] = "\n# comment\ninvalid\n:x:1:user\nnegative:x:-1:user\n"
                         "overflow:x:999999999999999999999:user\n"
                         "junk:x:12z:user\nextra:x:12:user:field\n"
                         "bad:x:12:a,,b\nbad:x:12:,a\nbad:x:12:a,\n"
                         "nul:x:7:alice\0,bob\n"
                         "staff:x:7:alice,bob\nempty:x:8:\nlast:x:9:carol";
  ASSERT_EQ(file.value()->write(records, sizeof(records) - 1).value,
            sizeof(records) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(path);
  char buffer[1025], second[1025];
  group entry{}, other{}, *result = nullptr;
  libc_errno = EDOM;
  ASSERT_EQ(
      LIBC_NAMESPACE::getgrnam_r("staff", &entry, buffer + 1, 1024, &result),
      0);
  ASSERT_EQ(result, &entry);
  EXPECT_EQ(entry.gr_gid, gid_t(7));
  EXPECT_STREQ(entry.gr_mem[0], "alice");
  EXPECT_STREQ(entry.gr_mem[1], "bob");
  EXPECT_EQ(entry.gr_mem[2], static_cast<char *>(nullptr));
  EXPECT_EQ(reinterpret_cast<uintptr_t>(entry.gr_mem) % alignof(char *),
            uintptr_t(0));
  ASSERT_EQ(
      LIBC_NAMESPACE::getgrgid_r(8, &other, second, sizeof(second), &result),
      0);
  ASSERT_EQ(result, &other);
  EXPECT_EQ(other.gr_mem[0], static_cast<char *>(nullptr));
  EXPECT_STREQ(entry.gr_mem[0], "alice");
  ASSERT_EQ(
      LIBC_NAMESPACE::getgrgid_r(9, &other, second, sizeof(second), &result),
      0);
  ASSERT_EQ(result, &other);
  EXPECT_STREQ(other.gr_mem[0], "carol");
  EXPECT_EQ(LIBC_NAMESPACE::getgrnam_r("missing", &entry, buffer,
                                       sizeof(buffer), &result),
            0);
  EXPECT_EQ(result, static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  auto *shared = LIBC_NAMESPACE::getgrnam("staff");
  ASSERT_TRUE(shared != nullptr);
  EXPECT_STREQ(shared->gr_mem[1], "bob");
  EXPECT_EQ(LIBC_NAMESPACE::getgrgid(8), shared);
  EXPECT_STREQ(shared->gr_name, "empty");
  EXPECT_EQ(shared->gr_mem[0], static_cast<char *>(nullptr));
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
  EXPECT_EQ(LIBC_NAMESPACE::getgrnam_r("staff", &entry, buffer, sizeof(buffer),
                                       &result),
            ENOENT);
  EXPECT_EQ(result, static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::getgrnam("staff"), static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOENT);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(nullptr);
}

TEST(LlvmLibcGroupQueryTest, PointerArrayBoundsAndLargeRecords) {
  const char *path = libc_make_test_file_path("group-large.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  constexpr char start[] = "many:x:42:";
  ASSERT_EQ(file.value()->write(start, sizeof(start) - 1).value,
            sizeof(start) - 1);
  for (unsigned i = 0; i < 512; ++i)
    ASSERT_EQ(file.value()->write(i == 511 ? "a\n" : "a,", 2).value, size_t(2));
  ASSERT_EQ(file.value()->close(), 0);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(path);
  auto *entry = LIBC_NAMESPACE::getgrgid(42);
  ASSERT_TRUE(entry != nullptr);
  for (unsigned i = 0; i < 512; ++i)
    EXPECT_STREQ(entry->gr_mem[i], "a");
  EXPECT_EQ(entry->gr_mem[512], static_cast<char *>(nullptr));
  group value{}, *result = &value;
  char buffer[2048];
  libc_errno = EDOM;
  EXPECT_EQ(
      LIBC_NAMESPACE::getgrgid_r(42, &value, buffer, sizeof(buffer), &result),
      ERANGE);
  EXPECT_EQ(result, static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::getgrnam_r(nullptr, &value, buffer, sizeof(buffer),
                                       &result),
            EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::getgrgid_r(42, &value, buffer, 0, &result), ERANGE);
  EXPECT_EQ(
      LIBC_NAMESPACE::getgrgid_r(42, nullptr, buffer, sizeof(buffer), &result),
      EINVAL);
  EXPECT_EQ(
      LIBC_NAMESPACE::getgrgid_r(42, &value, nullptr, sizeof(buffer), &result),
      EINVAL);
  EXPECT_EQ(
      LIBC_NAMESPACE::getgrgid_r(42, &value, buffer, sizeof(buffer), nullptr),
      EINVAL);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(nullptr);
}

TEST(LlvmLibcGroupQueryTest, ExactAlignmentAndCapacity) {
  group entry;
  for (size_t skew = 0; skew < alignof(char *); ++skew) {
    alignas(char *) char storage[128];
    char *buffer = storage + skew;
    const char text[] = "a:x:1:u";
    for (size_t size = sizeof(text); size < 64; ++size) {
      for (char &ch : storage)
        ch = 'Z';
      for (size_t i = 0; i < sizeof(text); ++i)
        buffer[i] = text[i];
      size_t pad = (-reinterpret_cast<uintptr_t>(buffer + sizeof(text))) %
                   alignof(char *);
      int expected =
          size >= sizeof(text) + pad + 2 * sizeof(char *) ? 0 : ERANGE;
      EXPECT_EQ(LIBC_NAMESPACE::group_db::parse_group(buffer, sizeof(text),
                                                      size, &entry),
                expected);
      for (size_t i = 0; i < skew; ++i)
        EXPECT_EQ(storage[i], 'Z');
      for (size_t i = skew + size; i < sizeof(storage); ++i)
        EXPECT_EQ(storage[i], 'Z');
    }
  }
}
