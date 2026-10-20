//===-- Group stream and supplementary list tests -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/endgrent.h"
#include "src/grp/fgetgrent.h"
#include "src/grp/fgetgrent_r.h"
#include "src/grp/getgrent.h"
#include "src/grp/getgrent_r.h"
#include "src/grp/getgrnam_r.h"
#include "src/grp/putgrent.h"
#include "src/grp/setgrent.h"
#include "src/grp/stream.h"
#include "src/stdio/remove.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;

TEST(LlvmLibcGroupStream, IterationRetryAndIndependentLookup) {
  const char *path = libc_make_test_file_path("group-stream");
  auto output = openfile(path, "w");
  ASSERT_TRUE(output.has_value());
  constexpr char records[] =
      "# comment\n\ninvalid\nbad:x:3:a,,b\nnul:x:4:a\0b\n"
      "first:x:7:alice,bob\nempty::8:\nlast:x:9:carol";
  ASSERT_EQ(output.value()->write(records, sizeof(records) - 1).value,
            sizeof(records) - 1);
  ASSERT_EQ(output.value()->close(), 0);
  group_db::TESTONLY_set_lookup_path(path);
  setgrent();
  group entry{}, *result = nullptr;
  char buffer[513];
  libc_errno = EDOM;
  EXPECT_EQ(getgrent_r(&entry, buffer, 1, &result), ERANGE);
  EXPECT_EQ(result, static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(getgrent_r(&entry, buffer + 1, 512, &result), 0);
  EXPECT_EQ(result, &entry);
  EXPECT_STREQ(entry.gr_name, "first");
  EXPECT_STREQ(entry.gr_mem[1], "bob");
  ASSERT_EQ(getgrnam_r("last", &entry, buffer, sizeof(buffer), &result), 0);
  EXPECT_STREQ(entry.gr_name, "last");
  auto *shared = getgrent();
  ASSERT_NE(shared, static_cast<group *>(nullptr));
  EXPECT_STREQ(shared->gr_name, "empty");
  EXPECT_EQ(shared->gr_mem[0], static_cast<char *>(nullptr));
  ASSERT_EQ(getgrent_r(&entry, buffer, sizeof(buffer), &result), 0);
  EXPECT_STREQ(entry.gr_name, "last");
  EXPECT_EQ(getgrent_r(&entry, buffer, sizeof(buffer), &result), ENOENT);
  EXPECT_EQ(result, static_cast<group *>(nullptr));
  EXPECT_EQ(getgrent(), static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  setgrent();
  shared = getgrent();
  ASSERT_NE(shared, static_cast<group *>(nullptr));
  EXPECT_STREQ(shared->gr_name, "first");
  endgrent();
  shared = getgrent();
  ASSERT_NE(shared, static_cast<group *>(nullptr));
  EXPECT_STREQ(shared->gr_name, "first");
  endgrent();
  endgrent();
  group_db::TESTONLY_set_lookup_path(nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcGroupStream, ExplicitStreamRoundTripAndErrors) {
  const char *path = libc_make_test_file_path("group-roundtrip");
  auto output = openfile(path, "w+");
  ASSERT_TRUE(output.has_value());
  auto *stream = reinterpret_cast<::FILE *>(output.value());
  char name[] = "team", password[] = "x", alice[] = "alice", bob[] = "bob";
  char *members[] = {alice, bob, nullptr};
  group original{name, password, 123, members};
  EXPECT_EQ(putgrent(&original, stream), 0);
  original.gr_mem = nullptr;
  EXPECT_EQ(putgrent(&original, stream), 0);
  ASSERT_TRUE(output.value()->seek(0, SEEK_SET).has_value());
  group entry{}, *result = nullptr;
  char buffer[256];
  libc_errno = EDOM;
  EXPECT_EQ(fgetgrent_r(stream, &entry, buffer, 16, &result), ERANGE);
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(fgetgrent_r(stream, &entry, buffer + 1, 255, &result), 0);
  EXPECT_STREQ(entry.gr_name, name);
  EXPECT_EQ(entry.gr_gid, gid_t(123));
  EXPECT_STREQ(entry.gr_mem[0], alice);
  EXPECT_STREQ(entry.gr_mem[1], bob);
  auto *shared = fgetgrent(stream);
  ASSERT_NE(shared, static_cast<group *>(nullptr));
  EXPECT_EQ(shared->gr_mem[0], static_cast<char *>(nullptr));
  EXPECT_EQ(fgetgrent_r(stream, &entry, buffer, sizeof(buffer), &result),
            ENOENT);
  EXPECT_EQ(result, static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  char invalid[] = "bad:name";
  original.gr_name = invalid;
  EXPECT_EQ(putgrent(&original, stream), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  original.gr_name = name;
  char bad_member[] = "injected\nrecord";
  members[1] = bad_member;
  original.gr_mem = members;
  EXPECT_EQ(putgrent(&original, stream), -1);
  EXPECT_EQ(output.value()->close(), 0);
  auto input = openfile(path, "r");
  ASSERT_TRUE(input.has_value());
  members[1] = bob;
  EXPECT_EQ(putgrent(&original, reinterpret_cast<::FILE *>(input.value())), -1);
  EXPECT_EQ(int(libc_errno), EBADF);
  EXPECT_EQ(input.value()->close(), 0);
  endgrent();
  EXPECT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcGroupStream, LargeRecordAndPointerSpaceRetry) {
  const char *path = libc_make_test_file_path("group-large-stream");
  auto output = openfile(path, "w");
  ASSERT_TRUE(output.has_value());
  ASSERT_EQ(output.value()->write("many:x:42:", 10).value, size_t(10));
  for (int i = 0; i < 512; ++i)
    ASSERT_EQ(output.value()->write(i == 511 ? "u\n" : "u,", 2).value,
              size_t(2));
  ASSERT_EQ(output.value()->close(), 0);
  auto input = openfile(path, "r");
  ASSERT_TRUE(input.has_value());
  auto *stream = reinterpret_cast<::FILE *>(input.value());
  group entry{}, *result = nullptr;
  char small[2048];
  EXPECT_EQ(fgetgrent_r(stream, &entry, small, sizeof(small), &result), ERANGE);
  auto *shared = fgetgrent(stream);
  ASSERT_NE(shared, static_cast<group *>(nullptr));
  for (int i = 0; i < 512; ++i)
    EXPECT_STREQ(shared->gr_mem[i], "u");
  EXPECT_EQ(shared->gr_mem[512], static_cast<char *>(nullptr));
  EXPECT_EQ(input.value()->close(), 0);
  endgrent();
  EXPECT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcGroupStream, BackendErrorsAndInvalidArguments) {
  File file(
      [](File *, const void *, size_t) -> FileIOResult { return {0, ENOSPC}; },
      [](File *, void *, size_t) -> FileIOResult { return {0, EIO}; },
      [](File *, off_t, int) -> ErrorOr<off_t> { return Error(ESPIPE); },
      [](File *) { return 0; }, nullptr, 0, _IONBF, false,
      File::mode_flags("r+"));
  auto *stream = reinterpret_cast<::FILE *>(&file);
  group entry{}, *result = &entry;
  char buffer[128];
  libc_errno = EDOM;
  EXPECT_EQ(fgetgrent_r(stream, &entry, buffer, sizeof(buffer), &result), EIO);
  EXPECT_EQ(result, static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(fgetgrent_r(stream, &entry, buffer, 0, &result), ERANGE);
  EXPECT_EQ(fgetgrent_r(stream, nullptr, buffer, sizeof(buffer), &result),
            EINVAL);
  EXPECT_EQ(fgetgrent_r(stream, &entry, buffer, sizeof(buffer), nullptr),
            EINVAL);
  char name[] = "team", password[] = "x";
  group output{name, password, 123, nullptr};
  EXPECT_EQ(putgrent(&output, stream), -1);
  EXPECT_EQ(int(libc_errno), ENOSPC);
  group_db::TESTONLY_set_lookup_path("/nonexistent/group/database");
  endgrent();
  libc_errno = EDOM;
  EXPECT_EQ(getgrent_r(&entry, buffer, sizeof(buffer), &result), ENOENT);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(getgrent(), static_cast<group *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOENT);
  group_db::TESTONLY_set_lookup_path(nullptr);
}
