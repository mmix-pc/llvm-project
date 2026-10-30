//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit tests for getpwent, setpwent, and endpwent.
///
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/types/struct_passwd.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/pwd/endpwent.h"
#include "src/pwd/getpwent.h"
#include "src/pwd/pwd_utils.h"
#include "src/pwd/setpwent.h"
#include "src/stdio/remove.h"
#include "src/string/string_utils.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::testing::ErrnoSetterMatcher;

namespace {

// RAII helper class for creating and automatically removing temporary test
// files.
class HermeticFile {
  char path[256];

public:
  HermeticFile(const char *file_path, const char *content, size_t length)
      : path{} {
    LIBC_NAMESPACE::internal::strlcpy(path, file_path, sizeof(path));

    auto file_or = LIBC_NAMESPACE::openfile(path, "w");
    if (file_or.has_value()) {
      auto *f = file_or.value();
      f->write(content, length);
      f->close();
    }
  }

  HermeticFile(const char *file_path, const char *content)
      : HermeticFile(file_path, content,
                     LIBC_NAMESPACE::internal::string_length(content)) {}

  ~HermeticFile() {
    LIBC_NAMESPACE::endpwent();
    LIBC_NAMESPACE::remove(path);
  }

  const char *get_path() const { return path; }
};

class LlvmLibcPwdTest : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
protected:
  void checkLongRecord(size_t length, bool newline) {
    constexpr char prefix[] = "long:x:42:43:";
    constexpr char suffix[] = ":/home/long:/bin/sh";
    char content[8192];
    ASSERT_LT(length + 1, sizeof(content));
    size_t pos = 0;
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
      content[pos++] = prefix[i];
    size_t gecos_length = length - (sizeof(prefix) - 1) - (sizeof(suffix) - 1);
    for (size_t i = 0; i < gecos_length; ++i)
      content[pos++] = 'g';
    for (size_t i = 0; i < sizeof(suffix) - 1; ++i)
      content[pos++] = suffix[i];
    ASSERT_EQ(pos, length);
    if (newline)
      content[pos++] = '\n';
    HermeticFile test_file(libc_make_test_file_path("getpwent_long.test"),
                           content, pos);
    LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());
    libc_errno = EDOM;
    auto *entry = LIBC_NAMESPACE::getpwent();
    ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
    EXPECT_STREQ(entry->pw_name, "long");
    EXPECT_STREQ(entry->pw_passwd, "x");
    EXPECT_EQ(entry->pw_uid, uid_t(42));
    EXPECT_EQ(entry->pw_gid, gid_t(43));
    EXPECT_EQ(LIBC_NAMESPACE::internal::string_length(entry->pw_gecos),
              gecos_length);
    for (size_t i = 0; i < gecos_length; ++i)
      ASSERT_EQ(entry->pw_gecos[i], 'g');
    EXPECT_STREQ(entry->pw_dir, "/home/long");
    EXPECT_STREQ(entry->pw_shell, "/bin/sh");
    ASSERT_ERRNO_EQ(EDOM);
    EXPECT_EQ(LIBC_NAMESPACE::getpwent(),
              static_cast<struct passwd *>(nullptr));
    ASSERT_ERRNO_SUCCESS();
  }
};

} // namespace

TEST_F(LlvmLibcPwdTest, GetPwentTestSuccess) {
  const char *content = "root:x:0:0:root:/root:/bin/bash\n"
                        "bin:x:1:1:bin:/bin:/sbin/nologin\n";
  HermeticFile test_file(libc_make_test_file_path("getpwent_success.test"),
                         content);

  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());
  LIBC_NAMESPACE::setpwent();

  struct passwd *pwd1 = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd1 != nullptr);
  ASSERT_STREQ(pwd1->pw_name, "root");
  ASSERT_EQ(pwd1->pw_uid, 0u);

  struct passwd *pwd2 = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd2 != nullptr);
  ASSERT_STREQ(pwd2->pw_name, "bin");
  ASSERT_EQ(pwd2->pw_uid, 1u);

  struct passwd *pwd3 = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd3 == nullptr);

  LIBC_NAMESPACE::endpwent();
}

TEST_F(LlvmLibcPwdTest, MalformedOnlyReachesEof) {
  const char *content = "invalid_line_without_enough_fields\n";
  HermeticFile test_file(libc_make_test_file_path("getpwent_fail.test"),
                         content);

  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());
  LIBC_NAMESPACE::setpwent();

  libc_errno = EDOM;
  struct passwd *pwd = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd == nullptr);
  ASSERT_ERRNO_EQ(EDOM);

  LIBC_NAMESPACE::endpwent();
}

TEST_F(LlvmLibcPwdTest, BlankPrefixDoesNotMeanEof) {
  HermeticFile test_file(libc_make_test_file_path("getpwent_blank.test"),
                         "\n\nroot:x:0:0:root:/root:/bin/sh\n");
  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());
  libc_errno = EDOM;
  auto *entry = LIBC_NAMESPACE::getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "root");
  ASSERT_ERRNO_EQ(EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::getpwent(), static_cast<struct passwd *>(nullptr));
  ASSERT_ERRNO_SUCCESS();
}

TEST_F(LlvmLibcPwdTest, SkipsInvalidRecordsBeforeValidFinalLine) {
  constexpr char content[] = "#comment:x:1:1:comment:/:/bin/sh\n"
                             "malformed\n"
                             "baduid:x:-1:2:bad:/:/bin/sh\n"
                             "badgid:x:1:2z:bad:/:/bin/sh\n"
                             "extra:x:1:2:bad:/:/bin/sh:extra\n"
                             "nul:x:1:2:bad:/:/bin/sh\0hidden\n"
                             "\nvalid:x:123:456:Valid:/home/valid:/bin/sh";
  HermeticFile test_file(libc_make_test_file_path("getpwent_skip.test"),
                         content, sizeof(content) - 1);
  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());
  libc_errno = EDOM;
  auto *entry = LIBC_NAMESPACE::getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "valid");
  EXPECT_EQ(entry->pw_uid, uid_t(123));
  EXPECT_EQ(entry->pw_gid, gid_t(456));
  EXPECT_STREQ(entry->pw_shell, "/bin/sh");
  EXPECT_EQ(LIBC_NAMESPACE::getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::getpwent(), static_cast<struct passwd *>(nullptr));
  ASSERT_ERRNO_EQ(EDOM);
}

TEST_F(LlvmLibcPwdTest, BlankAndInvalidTailReachesEof) {
  HermeticFile test_file(libc_make_test_file_path("getpwent_tail.test"),
                         "root:x:0:0:root:/root:/bin/sh\n\n#comment\ninvalid");
  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());
  ASSERT_NE(LIBC_NAMESPACE::getpwent(), static_cast<struct passwd *>(nullptr));
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::getpwent(), static_cast<struct passwd *>(nullptr));
  ASSERT_ERRNO_EQ(EDOM);
}

TEST_F(LlvmLibcPwdTest, EmptyFileReachesEof) {
  HermeticFile test_file(libc_make_test_file_path("getpwent_empty.test"), "");
  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::getpwent(), static_cast<struct passwd *>(nullptr));
  ASSERT_ERRNO_EQ(EDOM);
}

TEST_F(LlvmLibcPwdTest, LongRecordsAndOldBufferBoundary) {
  constexpr size_t lengths[] = {1022, 1023, 1024, 4096};
  for (size_t length : lengths) {
    checkLongRecord(length, true);
    checkLongRecord(length, false);
  }
}

TEST_F(LlvmLibcPwdTest, ResetAndCloseRetainBorrowedResult) {
  HermeticFile test_file(libc_make_test_file_path("getpwent_lifetime.test"),
                         "root:x:0:0:root:/root:/bin/sh\n");
  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());
  auto *entry = LIBC_NAMESPACE::getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  char *name = entry->pw_name;
  char *shell = entry->pw_shell;
  libc_errno = EDOM;
  LIBC_NAMESPACE::setpwent();
  EXPECT_EQ(entry->pw_name, name);
  EXPECT_STREQ(name, "root");
  EXPECT_STREQ(shell, "/bin/sh");
  LIBC_NAMESPACE::endpwent();
  LIBC_NAMESPACE::endpwent();
  EXPECT_EQ(entry->pw_shell, shell);
  EXPECT_STREQ(name, "root");
  EXPECT_STREQ(shell, "/bin/sh");
  ASSERT_ERRNO_EQ(EDOM);
  ASSERT_EQ(LIBC_NAMESPACE::getpwent(), entry);
  EXPECT_STREQ(entry->pw_name, "root");
}

TEST_F(LlvmLibcPwdTest, SetPwentTestHermetic) {
  const char *content = "user1:x:1000:1000:User One:/home/user1:/bin/bash\n"
                        "user2:x:1001:1001:User Two:/home/user2:/bin/bash\n";
  HermeticFile test_file(libc_make_test_file_path("setpwent_hermetic.test"),
                         content);

  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());

  struct passwd *pwd = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd != nullptr);
  ASSERT_STREQ(pwd->pw_name, "user1");

  pwd = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd != nullptr);
  ASSERT_STREQ(pwd->pw_name, "user2");

  // Reset iteration
  LIBC_NAMESPACE::setpwent();

  pwd = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd != nullptr);
  ASSERT_STREQ(pwd->pw_name, "user1");

  LIBC_NAMESPACE::endpwent();
}

TEST_F(LlvmLibcPwdTest, ReopenAfterEndpwent) {
  const char *content = "root:x:0:0:root:/root:/bin/bash\n";
  HermeticFile test_file(libc_make_test_file_path("reopen_endpwent.test"),
                         content);

  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(test_file.get_path());

  struct passwd *pwd = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd != nullptr);
  ASSERT_STREQ(pwd->pw_name, "root");

  LIBC_NAMESPACE::endpwent();

  pwd = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd != nullptr);
  ASSERT_STREQ(pwd->pw_name, "root");

  LIBC_NAMESPACE::endpwent();
}

TEST_F(LlvmLibcPwdTest, FileOpenFailure) {
  LIBC_NAMESPACE::passwd::TESTONLY_set_passwd_path(
      "/nonexistent_directory/nonexistent_file");
  LIBC_NAMESPACE::endpwent(); // Force close any existing file

  struct passwd *pwd = LIBC_NAMESPACE::getpwent();
  ASSERT_TRUE(pwd == nullptr);
  ASSERT_ERRNO_EQ(ENOENT);
}
