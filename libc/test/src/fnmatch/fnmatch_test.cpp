//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unittests for fnmatch.
///
//===----------------------------------------------------------------------===//

#include "hdr/fnmatch_macros.h"
#include "src/fnmatch/fnmatch.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcFnmatchTest, LiteralsAndWildcards) {
  struct Case {
    const char *pattern;
    const char *text;
    int result;
  };
  const Case cases[] = {{"test", "test", 0},
                        {"", "", 0},
                        {"", "x", FNM_NOMATCH},
                        {"x", "", FNM_NOMATCH},
                        {"*", "anything", 0},
                        {"***", "", 0},
                        {"a?c", "abc", 0},
                        {"a?c", "ac", FNM_NOMATCH},
                        {"a*b*c", "abbbc", 0},
                        {"*ab*bc", "abababc", 0},
                        {"*a", "baab", FNM_NOMATCH},
                        {"a", "aa", FNM_NOMATCH},
                        {"?", "\xff", 0},
                        {"\xff", "\xff", 0}};
  for (const auto &c : cases)
    EXPECT_EQ(LIBC_NAMESPACE::fnmatch(c.pattern, c.text, 0), c.result);
}

TEST(LlvmLibcFnmatchTest, PathnameAndLeadingDirectory) {
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a*b", "a/b", 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a*b", "a/b", FNM_PATHNAME), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a?b", "a/b", FNM_PATHNAME), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a[/]b", "a/b", FNM_PATHNAME), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a/*/c", "a/b/c", FNM_PATHNAME), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a/*/c", "a/b/d/c", FNM_PATHNAME),
            FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a", "a/b", FNM_LEADING_DIR), 0);
  EXPECT_EQ(
      LIBC_NAMESPACE::fnmatch("a*", "abc/d", FNM_PATHNAME | FNM_LEADING_DIR),
      0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a", "ab/c", FNM_LEADING_DIR), FNM_NOMATCH);
}

TEST(LlvmLibcFnmatchTest, LeadingPeriods) {
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("*?[!a]", "a.", FNM_PERIOD), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("*", ".x", 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("*", ".x", FNM_PERIOD), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("?x", ".x", FNM_PERIOD), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("[.]x", ".x", FNM_PERIOD), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch(".*", ".x", FNM_PERIOD), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("*.x", ".x", FNM_PERIOD), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a/*", "a/.x", FNM_PERIOD), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a/*", "a/.x", FNM_PERIOD | FNM_PATHNAME),
            FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a/.*", "a/.x", FNM_PERIOD | FNM_PATHNAME),
            0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("*/*x", "a/.x", FNM_PERIOD | FNM_PATHNAME),
            FNM_NOMATCH);
}

TEST(LlvmLibcFnmatchTest, EscapesAndCaseFolding) {
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a\\*b", "a*b", 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a\\*b", "a*b", FNM_NOESCAPE), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("a\\*b", "a\\xxb", FNM_NOESCAPE), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("\\", "\\", 0), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("\\", "\\", FNM_NOESCAPE), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("\\?", "?", 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("\\.", ".", FNM_PERIOD), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("ABC", "abc", 0), FNM_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("ABC", "abc", FNM_CASEFOLD), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch("[A-Z]", "q", FNM_CASEFOLD), 0);
}

TEST(LlvmLibcFnmatchTest, Brackets) {
  struct Case {
    const char *pattern;
    const char *text;
    int result;
  };
  const Case cases[] = {{"[a-c]", "b", 0},
                        {"[!a-c]", "d", 0},
                        {"[^a-c]", "b", FNM_NOMATCH},
                        {"[]a]", "]", 0},
                        {"[-a]", "-", 0},
                        {"[a-]", "-", 0},
                        {"[a\\-c]", "-", 0},
                        {"[a\\]]", "]", 0},
                        {"[\\!]", "!", 0},
                        {"[", "[", 0},
                        {"[ab", "[ab", 0},
                        {"[]", "[]", 0},
                        {"[a", "a", FNM_NOMATCH},
                        {"[z-a]", "b", FNM_NOMATCH},
                        {"[[:unknown:]]", "a", FNM_NOMATCH},
                        {"[![:unknown:]]", "a", FNM_NOMATCH},
                        {"[[:alpha:]", "a", FNM_NOMATCH},
                        {"[a\\", "a", FNM_NOMATCH},
                        {"[[.a.]]", "a", 0},
                        {"[[=a=]]", "a", 0},
                        {"[[.a.]-[.c.]]", "b", 0},
                        {"[[.ab.]]", "a", FNM_NOMATCH}};
  for (const auto &c : cases)
    EXPECT_EQ(LIBC_NAMESPACE::fnmatch(c.pattern, c.text, 0), c.result);
}

TEST(LlvmLibcFnmatchTest, CharacterClasses) {
  const char *patterns[] = {"[[:alnum:]]", "[[:alpha:]]", "[[:blank:]]",
                            "[[:cntrl:]]", "[[:digit:]]", "[[:graph:]]",
                            "[[:lower:]]", "[[:print:]]", "[[:punct:]]",
                            "[[:space:]]", "[[:upper:]]", "[[:xdigit:]]"};
  const char *matches[] = {"9", "a", "\t", "\n", "4", "!",
                           "z", " ", "?",  "\r", "Q", "f"};
  const char *misses[] = {"!", "1",  "a", "a", "a", " ",
                          "Z", "\n", "a", "a", "q", "g"};
  for (unsigned i = 0; i < 12; ++i) {
    EXPECT_EQ(LIBC_NAMESPACE::fnmatch(patterns[i], matches[i], 0), 0);
    EXPECT_EQ(LIBC_NAMESPACE::fnmatch(patterns[i], misses[i], 0), FNM_NOMATCH);
    EXPECT_EQ(LIBC_NAMESPACE::fnmatch(patterns[i], "\xff", 0), FNM_NOMATCH);
  }
}

TEST(LlvmLibcFnmatchTest, BoundedBacktrackingStorage) {
  char pattern[1026];
  char text[1026];
  for (unsigned i = 0; i < 512; ++i) {
    pattern[2 * i] = '*';
    pattern[2 * i + 1] = 'a';
  }
  pattern[1024] = 'b';
  pattern[1025] = 0;
  for (unsigned i = 0; i < 1024; ++i)
    text[i] = 'a';
  text[1024] = 'b';
  text[1025] = 0;
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch(pattern, text, 0), 0);
  text[1024] = 'c';
  EXPECT_EQ(LIBC_NAMESPACE::fnmatch(pattern, text, 0), FNM_NOMATCH);
}
