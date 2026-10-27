//===-- Pathname collation tie-breaking tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/string/strxfrm.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
static bool distinct_keys;
static size_t test_strxfrm(char *out, const char *path, size_t capacity) {
  if (capacity) {
    out[0] = distinct_keys ? (*path == 'z' ? 'a' : 'b') : 'a';
    if (capacity > 1)
      out[1] = '\0';
  }
  return 1;
}
} // namespace LIBC_NAMESPACE_DECL

// Supply controlled equal keys to the production sorter, independently of
// directory enumeration order and the host's installed locale database.
#define strxfrm test_strxfrm
#include "src/glob/glob.cpp"
#undef strxfrm

TEST(LlvmLibcGlobSortTest, EqualKeysUseUnsignedPathBytes) {
  LIBC_NAMESPACE::distinct_keys = false;
  char high[] = "\xff", middle[] = "\x80", low[] = "a";
  char *paths[] = {high, middle, low};
  ASSERT_EQ(LIBC_NAMESPACE::sort_paths(paths, 3), 0);
  EXPECT_EQ(paths[0], low);
  EXPECT_EQ(paths[1], middle);
  EXPECT_EQ(paths[2], high);
}

TEST(LlvmLibcGlobSortTest, PrefixAndIdenticalNames) {
  LIBC_NAMESPACE::distinct_keys = false;
  char longer[] = "ab", shorter[] = "a", same[] = "a";
  char *paths[] = {longer, shorter, same};
  ASSERT_EQ(LIBC_NAMESPACE::sort_paths(paths, 3), 0);
  EXPECT_STREQ(paths[0], "a");
  EXPECT_STREQ(paths[1], "a");
  EXPECT_EQ(paths[2], longer);
}

TEST(LlvmLibcGlobSortTest, CollationPrecedesPathBytes) {
  LIBC_NAMESPACE::distinct_keys = true;
  char first[] = "a", last[] = "z";
  char *paths[] = {first, last};
  ASSERT_EQ(LIBC_NAMESPACE::sort_paths(paths, 2), 0);
  EXPECT_EQ(paths[0], last);
  EXPECT_EQ(paths[1], first);
}
