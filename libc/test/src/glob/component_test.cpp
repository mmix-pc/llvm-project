//===-- Glob component parsing tests --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/glob/component.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;

TEST(LlvmLibcGlobComponentTest, SyntaxAndQuoting) {
  struct Case {
    const char *pattern;
    const char *prepared;
    bool magic;
    bool slash = false;
    bool noescape = false;
  } cases[] = {{"a*", "a*", true},
               {"a?", "a?", true},
               {"[x]", "[x]", true},
               {"[]]", "[]]", true},
               {"[!]]", "[!]]", true},
               {"[]", "[]", false},
               {"[!", "[!", false},
               {"[x", "[x", false},
               {"\\*", "*", false},
               {"\\[x]", "[x]", false},
               {"\\\\", "\\", false},
               {"a\\", "a\\", false},
               {"a\\", "a", false, true},
               {"*\\", "*", true, true},
               {"*\\\\", "*\\\\", true, true},
               {"\\*", "\\*", true, false, true},
               {"a\\", "a\\", false, true, true},
               {"\xc3\xa4", "\xc3\xa4", false},
               {"\\\xc3\xa4", "\xc3\xa4", false}};
  for (auto &entry : cases) {
    char buffer[32];
    size_t length = 0;
    do {
      buffer[length] = entry.pattern[length];
    } while (entry.pattern[length++]);
    auto parsed =
        prepare_glob_component(buffer, length - 1, entry.noescape, entry.slash,
                               RestartableConverter(true));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->magic, entry.magic);
    EXPECT_STREQ(buffer, entry.prepared);
    EXPECT_EQ(buffer[parsed->length], '\0');
  }
}

TEST(LlvmLibcGlobComponentTest, RejectsInvalidEncodingBeforeDequoting) {
  char invalid[] = "\\*\xff";
  auto parsed = prepare_glob_component(invalid, sizeof(invalid) - 1, false,
                                       false, RestartableConverter(true));
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error(), EILSEQ);
  EXPECT_STREQ(invalid, "\\*\xff");
  char incomplete[] = "*\xc3";
  parsed = prepare_glob_component(incomplete, sizeof(incomplete) - 1, false,
                                  false, RestartableConverter(true));
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error(), EILSEQ);
  EXPECT_STREQ(incomplete, "*\xc3");
  parsed = prepare_glob_component(invalid, sizeof(invalid) - 1, false, false,
                                  RestartableConverter(false));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_FALSE(parsed->magic);
  EXPECT_STREQ(invalid, "*\xff");
}
