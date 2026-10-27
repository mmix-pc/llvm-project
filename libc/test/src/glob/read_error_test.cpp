//===-- Directory read failure and partial glob results -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/glob_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/File/dir.h"
#include "src/__support/libc_errno.h"
#include "src/glob/globfree.h"
#include "src/sys/stat/lstat.h"
#include "src/sys/stat/stat.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
unsigned reads, closes, callbacks;
bool abort_requested;
int observed_error;
char observed_path[32];

class FaultDirectory {
public:
  static ErrorOr<FaultDirectory *> open(const char *) {
    static FaultDirectory directory;
    reads = 0;
    return &directory;
  }
  ErrorOr<struct dirent *> read() {
    static struct dirent entry = {};
    if (reads++)
      return Error(EIO);
    entry.d_name[0] = 'a';
    entry.d_name[1] = '\0';
    return &entry;
  }
  int close(bool) {
    ++closes;
    return 0;
  }
};
int test_stat(const char *, struct stat *result) {
  *result = {};
  result->st_mode = S_IFREG | 0600;
  return 0;
}
int on_error(const char *path, int error) {
  ++callbacks;
  observed_error = error;
  size_t i = 0;
  for (; path[i] && i + 1 < sizeof(observed_path); ++i)
    observed_path[i] = path[i];
  observed_path[i] = '\0';
  return abort_requested;
}
} // namespace
} // namespace LIBC_NAMESPACE_DECL

// A deterministic provider yields one real walker input, then fails. This
// does not rely on filesystem enumeration order or alter production code.
#define Dir FaultDirectory
#define lstat test_stat
#define stat(...) test_stat(__VA_ARGS__)
#include "src/glob/glob.cpp"
#undef stat
#undef lstat
#undef Dir

TEST(LlvmLibcGlobReadErrorTest, PartialResultsAndAppendSurviveReadFailure) {
  using namespace LIBC_NAMESPACE;
  for (int mode = 0; mode != 4; ++mode) {
    for (int append = 0; append != 2; ++append) {
      glob_t result = {};
      result.gl_offs = 2;
      if (append)
        ASSERT_EQ(LIBC_NAMESPACE::glob("old", GLOB_DOOFFS, nullptr, &result), 0);
      char *prior = append ? result.gl_pathv[2] : nullptr;
      callbacks = closes = reads = 0;
      abort_requested = mode == 2;
      int flags = GLOB_DOOFFS | (append ? GLOB_APPEND : 0) |
                  (mode == 1 || mode == 3 ? GLOB_ERR : 0);
      int status = LIBC_NAMESPACE::glob("dir/*", flags,
                                        mode == 3 ? nullptr : on_error, &result);
      EXPECT_EQ(status, mode == 0 ? 0 : GLOB_ABORTED);
      EXPECT_EQ(closes, 1U);
      EXPECT_EQ(reads, 2U);
      EXPECT_EQ(callbacks, mode == 3 ? 0U : 1U);
      if (mode != 3) {
        EXPECT_EQ(observed_error, EIO);
        EXPECT_STREQ(observed_path, "dir/");
      }
      ASSERT_EQ(result.gl_pathc, append ? size_t(2) : size_t(1));
      EXPECT_EQ(result.gl_pathv[0], static_cast<char *>(nullptr));
      EXPECT_EQ(result.gl_pathv[1], static_cast<char *>(nullptr));
      if (append) {
        EXPECT_EQ(result.gl_pathv[2], prior);
        EXPECT_STREQ(prior, "old");
      }
      EXPECT_STREQ(result.gl_pathv[append ? 3 : 2], "dir/a");
      EXPECT_EQ(result.gl_pathv[2 + result.gl_pathc], static_cast<char *>(nullptr));
      libc_errno = EDOM;
      LIBC_NAMESPACE::globfree(&result);
      EXPECT_EQ(int(libc_errno), EDOM);
    }
  }
}
