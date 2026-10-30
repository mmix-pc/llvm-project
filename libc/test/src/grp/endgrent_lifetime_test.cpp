//===-- Group borrowed-result lifetime tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fcntl_macros.h"
#include "src/fcntl/fcntl.h"
#include "src/grp/endgrent.h"
#include "src/grp/getgrent.h"
#include "src/grp/getgrgid.h"
#include "src/grp/getgrnam.h"
#include "src/grp/setgrent.h"
#include "src/grp/stream.h"
#include "src/stdio/remove.h"
#include "src/string/string_utils.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;

namespace {
class LlvmLibcGroupEndgrentLifetimeTest : public testing::Test {
protected:
  const char *path = libc_make_test_file_path("group-endgrent-lifetime");

  void SetUp() override {
    endgrent();
    auto output = openfile(path, "w");
    ASSERT_TRUE(output.has_value());
    constexpr char records[] =
        "staff:x:7:alice,bob\nempty::8:\nlast:!:9:carol\n";
    auto written = output.value()->write(records, sizeof(records) - 1);
    int error = output.value()->close();
    ASSERT_EQ(written.error, 0);
    ASSERT_EQ(written.value, sizeof(records) - 1);
    ASSERT_EQ(error, 0);
    group_db::TESTONLY_set_lookup_path(path);
  }

  void TearDown() override {
    endgrent();
    group_db::TESTONLY_set_lookup_path(nullptr);
    EXPECT_EQ(LIBC_NAMESPACE::remove(path), 0);
  }

  void checkLifetime(group *borrowed) {
    ASSERT_NE(borrowed, static_cast<group *>(nullptr));
    const group saved = *borrowed;
    ASSERT_NE(saved.gr_name, static_cast<char *>(nullptr));
    ASSERT_NE(saved.gr_passwd, static_cast<char *>(nullptr));
    ASSERT_NE(saved.gr_mem, static_cast<char **>(nullptr));
    char name[32], password[32], members[2][32];
    ASSERT_LT(internal::strlcpy(name, saved.gr_name, sizeof(name)), sizeof(name));
    ASSERT_LT(internal::strlcpy(password, saved.gr_passwd, sizeof(password)),
              sizeof(password));
    char *member_pointers[2];
    size_t count = 0;
    while (saved.gr_mem[count]) {
      ASSERT_LT(count, size_t(2));
      member_pointers[count] = saved.gr_mem[count];
      ASSERT_LT(internal::strlcpy(members[count], saved.gr_mem[count],
                                 sizeof(members[count])),
                sizeof(members[count]));
      ++count;
    }

    // No intervening nonreentrant query: only endgrent may affect the borrow.
    const int sentinels[] = {0, EDOM, ENOSYS};
    for (int sentinel : sentinels) {
      libc_errno = sentinel;
      endgrent();
      EXPECT_EQ(int(libc_errno), sentinel);
      ASSERT_EQ(borrowed->gr_name, saved.gr_name);
      ASSERT_EQ(borrowed->gr_passwd, saved.gr_passwd);
      ASSERT_EQ(borrowed->gr_mem, saved.gr_mem);
      EXPECT_EQ(borrowed->gr_gid, saved.gr_gid);
      EXPECT_STREQ(borrowed->gr_name, name);
      EXPECT_STREQ(borrowed->gr_passwd, password);
      for (size_t i = 0; i < count; ++i) {
        ASSERT_EQ(borrowed->gr_mem[i], member_pointers[i]);
        EXPECT_STREQ(borrowed->gr_mem[i], members[i]);
      }
      EXPECT_EQ(borrowed->gr_mem[count], static_cast<char *>(nullptr));
    }
  }
};
} // namespace

TEST_F(LlvmLibcGroupEndgrentLifetimeTest, KeyedNameWithOpenCursor) {
  setgrent();
  checkLifetime(getgrnam("staff"));
}

TEST_F(LlvmLibcGroupEndgrentLifetimeTest, KeyedIdWithoutCursor) {
  checkLifetime(getgrgid(7));
}

TEST_F(LlvmLibcGroupEndgrentLifetimeTest, EnumeratedRecord) {
  checkLifetime(getgrent());
}

TEST_F(LlvmLibcGroupEndgrentLifetimeTest, EmptyKeyedRecord) {
  checkLifetime(getgrgid(8));
}

TEST_F(LlvmLibcGroupEndgrentLifetimeTest, EmptyEnumeratedRecord) {
  ASSERT_NE(getgrent(), static_cast<group *>(nullptr));
  checkLifetime(getgrent());
}

TEST_F(LlvmLibcGroupEndgrentLifetimeTest, ClosesDescriptorAndReopensAtStart) {
  ASSERT_NE(getgrent(), static_cast<group *>(nullptr));
  auto cursor = group_db::enumeration_stream();
  ASSERT_TRUE(cursor.has_value());
  int fd = get_fileno(cursor.value());
  ASSERT_GE(fd, 0);
  ASSERT_GE(LIBC_NAMESPACE::fcntl(fd, F_GETFD), 0);
  libc_errno = EDOM;
  endgrent();
  EXPECT_EQ(int(libc_errno), EDOM);
  // Inspect the database's own descriptor without closing or reusing it.
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(fd, F_GETFD), -1);
  EXPECT_EQ(int(libc_errno), EBADF);
  libc_errno = ENOSYS;
  endgrent();
  EXPECT_EQ(int(libc_errno), ENOSYS);

  auto *reopened = getgrent();
  ASSERT_NE(reopened, static_cast<group *>(nullptr));
  EXPECT_STREQ(reopened->gr_name, "staff");
  EXPECT_EQ(reopened->gr_gid, gid_t(7));
}
