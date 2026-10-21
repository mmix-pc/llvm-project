//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/glob_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/glob/glob.h"
#include "src/glob/globfree.h"
#include "src/string/strcmp.h"
#include "test/UnitTest/Test.h"

// Exercise allocation failures in the production walker and result vector.
static bool inject = false;
static int remaining = -1;
static void *owned[1024];
static size_t live = 0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
static void forget(void *p) {
  for (size_t i = 0; i < live; ++i)
    if (owned[i] == p) {
      owned[i] = owned[--live];
      return;
    }
}
extern "C" void *__wrap_malloc(size_t n) {
  if (inject && remaining-- == 0)
    return nullptr;
  void *p = __real_malloc(n);
  if (inject && p)
    owned[live++] = p;
  return p;
}
extern "C" void *__wrap_realloc(void *old, size_t n) {
  if (inject && remaining-- == 0)
    return nullptr;
  void *p = __real_realloc(old, n);
  if (inject && p) {
    forget(old);
    owned[live++] = p;
  }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (inject)
    forget(p);
  __real_free(p);
}

namespace {
struct Result {
  glob_t value = {};
  ~Result() { LIBC_NAMESPACE::globfree(&value); }
  int expand(const char *p, int flags = 0,
             int (*error)(const char *, int) = nullptr) {
    return LIBC_NAMESPACE::glob(p, flags, error, &value);
  }
};
int calls, last_error;
int ignore_error(const char *, int error) {
  ++calls;
  last_error = error;
  return 0;
}
int abort_error(const char *path, int error) {
  ignore_error(path, error);
  return 1;
}
int fd_count() {
  int count = 0;
  for (int fd = 0; fd < 128; ++fd)
    count += LIBC_NAMESPACE::fcntl(fd, F_GETFD) >= 0;
  return count;
}
} // namespace

TEST(LlvmLibcGlob, SortAndAppendWithOffsets) {
  Result r;
  r.value.gl_offs = 2;
  ASSERT_EQ(r.expand("testdata/b.txt", GLOB_DOOFFS), 0);
  ASSERT_EQ(r.expand("testdata/[ab].txt", GLOB_DOOFFS | GLOB_APPEND), 0);
  ASSERT_EQ(r.value.gl_pathc, size_t(3));
  EXPECT_EQ(r.value.gl_pathv[0], static_cast<char *>(nullptr));
  EXPECT_EQ(r.value.gl_pathv[1], static_cast<char *>(nullptr));
  EXPECT_STREQ(r.value.gl_pathv[2], "testdata/b.txt");
  EXPECT_STREQ(r.value.gl_pathv[3], "testdata/a.txt");
  EXPECT_STREQ(r.value.gl_pathv[4], "testdata/b.txt");
  EXPECT_EQ(r.value.gl_pathv[5], static_cast<char *>(nullptr));
  EXPECT_EQ(r.expand("testdata/absent", GLOB_DOOFFS | GLOB_APPEND),
            GLOB_NOMATCH);
  EXPECT_EQ(r.value.gl_pathc, size_t(3));
}

TEST(LlvmLibcGlob, WildcardsAndLeadingDot) {
  Result r;
  ASSERT_EQ(r.expand("testdata/?.txt"), 0);
  ASSERT_EQ(r.value.gl_pathc, size_t(2));
  EXPECT_STREQ(r.value.gl_pathv[0], "testdata/a.txt");
  EXPECT_STREQ(r.value.gl_pathv[1], "testdata/b.txt");
  Result hidden;
  ASSERT_EQ(hidden.expand("testdata/.h*"), 0);
  EXPECT_EQ(hidden.value.gl_pathc, size_t(1));
  Result all;
  ASSERT_EQ(all.expand("testdata/*", GLOB_NOSORT), 0);
  for (size_t i = 0; i < all.value.gl_pathc; ++i)
    EXPECT_NE(LIBC_NAMESPACE::strcmp(all.value.gl_pathv[i], "testdata/.hidden"),
              0);
}

TEST(LlvmLibcGlob, RepeatedAppendGrowsVector) {
  Result r;
  ASSERT_EQ(r.expand("testdata/[ab].txt"), 0);
  for (size_t i = 1; i < 20; ++i) {
    ASSERT_EQ(r.expand("testdata/[ab].txt", GLOB_APPEND), 0);
    EXPECT_EQ(r.value.gl_pathc, 2 * (i + 1));
    EXPECT_STREQ(r.value.gl_pathv[2 * i], "testdata/a.txt");
    EXPECT_STREQ(r.value.gl_pathv[2 * i + 1], "testdata/b.txt");
    EXPECT_EQ(r.value.gl_pathv[2 * (i + 1)], static_cast<char *>(nullptr));
  }
}

TEST(LlvmLibcGlob, LongLiteralPathDoesNotRecurse) {
  char pattern[8194];
  for (size_t i = 0; i < 8192; i += 2) {
    pattern[i] = '.';
    pattern[i + 1] = '/';
  }
  pattern[8192] = '.';
  pattern[8193] = '\0';
  Result r;
  calls = last_error = 0;
  EXPECT_EQ(r.expand(pattern, GLOB_ERR, ignore_error), GLOB_ABORTED);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(last_error, ENAMETOOLONG);
}

TEST(LlvmLibcGlob, EscapesAndUnmatchedBracket) {
  Result literal;
  ASSERT_EQ(literal.expand("testdata/literal["), 0);
  EXPECT_STREQ(literal.value.gl_pathv[0], "testdata/literal[");
  Result star;
  ASSERT_EQ(star.expand("testdata/star\\*"), 0);
  EXPECT_STREQ(star.value.gl_pathv[0], "testdata/star*");
  Result escaped_slash;
  ASSERT_EQ(escaped_slash.expand("testdata\\/sub/leaf"), 0);
  EXPECT_STREQ(escaped_slash.value.gl_pathv[0], "testdata/sub/leaf");
  Result noescape;
  EXPECT_EQ(noescape.expand("testdata/star\\*", GLOB_NOESCAPE), GLOB_NOMATCH);
}

TEST(LlvmLibcGlob, LiteralNoMatchAndNoCheck) {
  Result missing;
  EXPECT_EQ(missing.expand("testdata/no-such-*"), GLOB_NOMATCH);
  EXPECT_EQ(missing.value.gl_pathc, size_t(0));
  Result fallback;
  ASSERT_EQ(fallback.expand("absent/\\*", GLOB_NOCHECK), 0);
  ASSERT_EQ(fallback.value.gl_pathc, size_t(1));
  EXPECT_STREQ(fallback.value.gl_pathv[0], "absent/\\*");
  Result empty;
  EXPECT_EQ(empty.expand(""), GLOB_NOMATCH);
  Result root;
  ASSERT_EQ(root.expand("/"), 0);
  EXPECT_STREQ(root.value.gl_pathv[0], "/");
}

TEST(LlvmLibcGlob, DirectoriesAndSymlinks) {
  Result nested;
  ASSERT_EQ(nested.expand("testdata/*/leaf"), 0);
  ASSERT_EQ(nested.value.gl_pathc, size_t(2));
  EXPECT_STREQ(nested.value.gl_pathv[0], "testdata/link/leaf");
  EXPECT_STREQ(nested.value.gl_pathv[1], "testdata/sub/leaf");
  Result marked;
  ASSERT_EQ(marked.expand("testdata/link", GLOB_MARK), 0);
  EXPECT_STREQ(marked.value.gl_pathv[0], "testdata/link/");
  Result slash;
  ASSERT_EQ(slash.expand("testdata/sub//", GLOB_MARK), 0);
  EXPECT_STREQ(slash.value.gl_pathv[0], "testdata/sub//");
  Result file_slash;
  EXPECT_EQ(file_slash.expand("testdata/a.txt/"), GLOB_NOMATCH);
  Result dangling;
  ASSERT_EQ(dangling.expand("testdata/dangling", GLOB_MARK), 0);
  EXPECT_STREQ(dangling.value.gl_pathv[0], "testdata/dangling");
}

TEST(LlvmLibcGlob, ErrorCallbackAndAbort) {
  Result r;
  calls = last_error = 0;
  EXPECT_EQ(r.expand("testdata/a.txt/*", 0, ignore_error), GLOB_NOMATCH);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(last_error, ENOTDIR);
  EXPECT_EQ(r.expand("testdata/a.txt/*", GLOB_ERR, ignore_error), GLOB_ABORTED);
  EXPECT_EQ(r.expand("testdata/a.txt/*", 0, abort_error), GLOB_ABORTED);
  EXPECT_EQ(r.expand("testdata/loop/*", GLOB_ERR), GLOB_ABORTED);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ELOOP);
  ASSERT_EQ(r.expand("testdata/a.txt"), 0);
  EXPECT_EQ(r.expand("testdata/a.txt/*", GLOB_APPEND | GLOB_ERR), GLOB_ABORTED);
  EXPECT_EQ(r.value.gl_pathc, size_t(1));
  EXPECT_STREQ(r.value.gl_pathv[0], "testdata/a.txt");
}

TEST(LlvmLibcGlob, OverflowAndFreePreserveErrno) {
  Result r;
  r.value.gl_offs = size_t(-1);
  EXPECT_EQ(r.expand("testdata/a.txt", GLOB_DOOFFS), GLOB_NOSPACE);
  EXPECT_EQ(r.value.gl_pathc, size_t(0));
  Result normal;
  ASSERT_EQ(normal.expand("testdata/a.txt"), 0);
  LIBC_NAMESPACE::libc_errno = EDOM;
  LIBC_NAMESPACE::globfree(&normal.value);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(normal.value.gl_pathv, static_cast<char **>(nullptr));
}

TEST(LlvmLibcGlob, AllocationFailuresReleasePartialResultsAndDirectories) {
  int descriptors = fd_count();
  bool success = false, partial = false;
  for (int failure = 0; failure < 256; ++failure) {
    remaining = failure;
    inject = true;
    glob_t result = {};
    int error = LIBC_NAMESPACE::glob("testdata/*", 0, nullptr, &result);
    partial |= error == GLOB_NOSPACE && result.gl_pathc != 0;
    LIBC_NAMESPACE::globfree(&result);
    inject = false;
    EXPECT_EQ(live, size_t(0));
    EXPECT_EQ(fd_count(), descriptors);
    if (!error) {
      success = true;
      break;
    }
    EXPECT_EQ(error, GLOB_NOSPACE);
  }
  EXPECT_TRUE(success);
  EXPECT_TRUE(partial);
}

TEST(LlvmLibcGlob, FailedAppendPreservesPreviousResults) {
  int descriptors = fd_count();
  bool success = false;
  for (int failure = 0; failure < 256; ++failure) {
    glob_t result = {};
    ASSERT_EQ(LIBC_NAMESPACE::glob("testdata/b.txt", 0, nullptr, &result), 0);
    remaining = failure;
    inject = true;
    int error =
        LIBC_NAMESPACE::glob("testdata/*.txt", GLOB_APPEND, nullptr, &result);
    bool preserved =
        result.gl_pathc >= 1 &&
        LIBC_NAMESPACE::strcmp(result.gl_pathv[0], "testdata/b.txt") == 0;
    LIBC_NAMESPACE::globfree(&result);
    inject = false;
    EXPECT_TRUE(preserved);
    EXPECT_EQ(live, size_t(0));
    EXPECT_EQ(fd_count(), descriptors);
    if (!error) {
      success = true;
      break;
    }
    EXPECT_EQ(error, GLOB_NOSPACE);
  }
  EXPECT_TRUE(success);
}
