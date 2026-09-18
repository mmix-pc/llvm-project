//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Basic round-trip tests for POSIX regex functions.
///
//===----------------------------------------------------------------------===//

#include "src/regex/regcomp.h"
#include "src/regex/regexec.h"
#include "src/regex/regfree.h"
#include "test/UnitTest/Test.h"

#include "hdr/regex_macros.h"
#include "hdr/types/regex_t.h"

TEST(LlvmLibcRegexTest, BasicLiteralRoundTrip) {
  regex_t preg;
  ASSERT_EQ(0,
            LIBC_NAMESPACE::regcomp(&preg, "hello", REG_EXTENDED | REG_NOSUB));
  ASSERT_EQ(0,
            LIBC_NAMESPACE::regexec(&preg, "say hello world", 0, nullptr, 0));
  ASSERT_EQ(REG_NOMATCH,
            LIBC_NAMESPACE::regexec(&preg, "goodbye", 0, nullptr, 0));
  LIBC_NAMESPACE::regfree(&preg);
}

TEST(LlvmLibcRegexTest, MismatchCases) {
  regex_t preg;
  // Partial match
  ASSERT_EQ(0,
            LIBC_NAMESPACE::regcomp(&preg, "hello", REG_EXTENDED | REG_NOSUB));
  ASSERT_EQ(REG_NOMATCH, LIBC_NAMESPACE::regexec(&preg, "hell", 0, nullptr, 0));
  LIBC_NAMESPACE::regfree(&preg);

  // Case sensitivity
  ASSERT_EQ(0,
            LIBC_NAMESPACE::regcomp(&preg, "Hello", REG_EXTENDED | REG_NOSUB));
  ASSERT_EQ(REG_NOMATCH,
            LIBC_NAMESPACE::regexec(&preg, "hello", 0, nullptr, 0));
  LIBC_NAMESPACE::regfree(&preg);

  // Empty string vs non-empty pattern
  ASSERT_EQ(0, LIBC_NAMESPACE::regcomp(&preg, "a", REG_EXTENDED | REG_NOSUB));
  ASSERT_EQ(REG_NOMATCH, LIBC_NAMESPACE::regexec(&preg, "", 0, nullptr, 0));
  LIBC_NAMESPACE::regfree(&preg);
}

TEST(LlvmLibcRegexTest, EmptyString) {
  regex_t preg;
  ASSERT_EQ(0, LIBC_NAMESPACE::regcomp(&preg, "", REG_EXTENDED | REG_NOSUB));
  ASSERT_EQ(0, LIBC_NAMESPACE::regexec(&preg, "anything", 0, nullptr, 0));
  ASSERT_EQ(0, LIBC_NAMESPACE::regexec(&preg, "", 0, nullptr, 0));
  LIBC_NAMESPACE::regfree(&preg);
}

TEST(LlvmLibcRegexTest, ExactMatch) {
  regex_t preg;
  ASSERT_EQ(0,
            LIBC_NAMESPACE::regcomp(&preg, "test", REG_EXTENDED | REG_NOSUB));
  ASSERT_EQ(0, LIBC_NAMESPACE::regexec(&preg, "test", 0, nullptr, 0));
  LIBC_NAMESPACE::regfree(&preg);
}

TEST(LlvmLibcRegexTest, NullByteStopsParsing) {
  regex_t preg;
  ASSERT_EQ(0,
            LIBC_NAMESPACE::regcomp(&preg, "match", REG_EXTENDED | REG_NOSUB));
  ASSERT_EQ(REG_NOMATCH,
            LIBC_NAMESPACE::regexec(&preg, "doesn't \0 match", 0, nullptr, 0));
  LIBC_NAMESPACE::regfree(&preg);
}

TEST(LlvmLibcRegexTest, ExtendedCapturesAndLongestMatch) {
  regex_t re;
  regmatch_t matches[4];
  ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "(a|aa)(b)?", REG_EXTENDED), 0);
  EXPECT_EQ(re.re_nsub, size_t(2));
  ASSERT_EQ(LIBC_NAMESPACE::regexec(&re, "zaab", 4, matches, 0), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(1));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(4));
  EXPECT_EQ(matches[1].rm_so, regoff_t(1));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(3));
  EXPECT_EQ(matches[2].rm_so, regoff_t(3));
  EXPECT_EQ(matches[2].rm_eo, regoff_t(4));
  EXPECT_EQ(matches[3].rm_so, regoff_t(-1));
  ASSERT_EQ(LIBC_NAMESPACE::regexec(&re, "zaa", 4, matches, 0), 0);
  EXPECT_EQ(matches[2].rm_so, regoff_t(-1));
  EXPECT_EQ(matches[2].rm_eo, regoff_t(-1));
  LIBC_NAMESPACE::regfree(&re);
}

TEST(LlvmLibcRegexTest, BasicCapturesAndBackreferences) {
  regex_t re;
  regmatch_t matches[3];
  ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "^\\([a-z]\\{2,3\\}\\)-\\1$", 0), 0);
  EXPECT_EQ(re.re_nsub, size_t(1));
  ASSERT_EQ(LIBC_NAMESPACE::regexec(&re, "abc-abc", 3, matches, 0), 0);
  EXPECT_EQ(matches[0].rm_eo, regoff_t(7));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(3));
  EXPECT_EQ(matches[2].rm_so, regoff_t(-1));
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "abc-ab", 0, nullptr, 0), REG_NOMATCH);
  LIBC_NAMESPACE::regfree(&re);
}

TEST(LlvmLibcRegexTest, CharacterClassesAndCase) {
  regex_t re;
  ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "^[[:alpha:]]+[[:digit:]]{2}$",
                                    REG_EXTENDED | REG_ICASE),
            0);
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "Ab09", 0, nullptr, 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "Ab9", 0, nullptr, 0), REG_NOMATCH);
  LIBC_NAMESPACE::regfree(&re);
  ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "^hello$", REG_ICASE), 0);
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "HELLO", 0, nullptr, 0), 0);
  LIBC_NAMESPACE::regfree(&re);
}

TEST(LlvmLibcRegexTest, NewlinesAndExecutionFlags) {
  regex_t re;
  ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "^a$", REG_NEWLINE), 0);
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "x\na\nx", 0, nullptr,
                                    REG_NOTBOL | REG_NOTEOL),
            0);
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "a", 0, nullptr, REG_NOTBOL),
            REG_NOMATCH);
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "a", 0, nullptr, REG_NOTEOL),
            REG_NOMATCH);
  LIBC_NAMESPACE::regfree(&re);
  const int newline_flags[] = {0, REG_NEWLINE};
  for (int flags : newline_flags) {
    ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "a.b", flags), 0);
    EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "a\nb", 0, nullptr, 0),
              flags ? REG_NOMATCH : 0);
    LIBC_NAMESPACE::regfree(&re);
    ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "a[^x]b", flags), 0);
    EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "a\nb", 0, nullptr, 0),
              flags ? REG_NOMATCH : 0);
    LIBC_NAMESPACE::regfree(&re);
  }
}

TEST(LlvmLibcRegexTest, EmptyAndNoSubexpressions) {
  const int compile_flags[] = {0, REG_EXTENDED, REG_NOSUB,
                               REG_EXTENDED | REG_NOSUB};
  for (int flags : compile_flags) {
    regex_t re;
    regmatch_t matches[2] = {{77, 88}, {99, 111}};
    ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "", flags), 0);
    ASSERT_EQ(LIBC_NAMESPACE::regexec(&re, "abc", 2, matches, 0), 0);
    EXPECT_EQ(matches[0].rm_so, regoff_t(flags & REG_NOSUB ? 77 : 0));
    EXPECT_EQ(matches[0].rm_eo, regoff_t(flags & REG_NOSUB ? 88 : 0));
    EXPECT_EQ(matches[1].rm_so, regoff_t(flags & REG_NOSUB ? 99 : -1));
    LIBC_NAMESPACE::regfree(&re);
    ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "a*", flags), 0);
    EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "bbb", 0, nullptr, 0), 0);
    LIBC_NAMESPACE::regfree(&re);
  }
}

TEST(LlvmLibcRegexTest, InvalidPatternsAndReuse) {
  const char *patterns[] = {"[", "(", "a\\", "*a", "[z-a]", "a{3,2}", "\\1"};
  const int errors[] = {REG_EBRACK, REG_EPAREN, REG_EESCAPE, REG_BADRPT,
                        REG_ERANGE, REG_BADBR,  REG_ESUBREG};
  regex_t re;
  for (unsigned i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
    EXPECT_EQ(LIBC_NAMESPACE::regcomp(&re, patterns[i], REG_EXTENDED),
              errors[i]);
    EXPECT_TRUE(re.__internal == nullptr);
    ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "a", 0), 0);
    EXPECT_EQ(LIBC_NAMESPACE::regexec(&re, "a", 0, nullptr, 0), 0);
    LIBC_NAMESPACE::regfree(&re);
    EXPECT_TRUE(re.__internal == nullptr);
  }
}
