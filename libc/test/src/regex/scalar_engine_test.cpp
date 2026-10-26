//===-- Private BSD regex scalar engine tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/freelist_heap.h"
#include "src/locale/locale.h"
#include "src/regex/bsd_regex.h"
#include "src/regex/character_regex.h"
#include "test/UnitTest/Test.h"
#include "test/src/__support/wchar/ISO2022CNFixture.h"
#include "test/src/locale/PatternCollationFixture.h"

namespace {
using Char = llvm_regex_char_t;
using LIBC_NAMESPACE::internal::CharacterRegex;
using LIBC_NAMESPACE::internal::RestartableConverter;
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0, attempts = 0;
bool fail() {
  ++attempts;
  return fail_after >= 0 && fail_after-- == 0;
}
struct Expression {
  llvm_regex_t value{};
  ~Expression() { __llvm_libc_bsd_regfree(&value); }
  template <size_t N> int compile(const Char (&pattern)[N], int flags = 0) {
    return __llvm_libc_bsd_regcomp_chars(&value, pattern, N - 1, flags);
  }
  template <size_t N>
  int match(const Char (&input)[N], size_t count, llvm_regmatch_t *matches,
            int flags = 0) {
    return __llvm_libc_bsd_regexec_chars(&value, input, N - 1, count, matches,
                                         flags);
  }
};
} // namespace

extern "C" void *__wrap_malloc(size_t size) {
  if (fail())
    return nullptr;
  void *p = heap.allocate(size);
  live += p != nullptr;
  return p;
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (fail())
    return nullptr;
  void *p = heap.realloc(old, size);
  live += p && !old;
  return p;
}
extern "C" void __wrap_free(void *p) {
  live -= p != nullptr;
  heap.free(p);
}

TEST(LlvmLibcRegexScalarTest, LiteralDotAndCharacterPositions) {
  Expression re;
  const Char pattern[] = {'(', 0x4e2d, '.', ')', '+', 0};
  ASSERT_EQ(re.compile(pattern, REG_EXTENDED), 0);
  const Char input[] = {'x', 0x4e2d, 0x1f600, 0x4e2d, 0xe9, 'z', 0};
  llvm_regmatch_t matches[3];
  ASSERT_EQ(re.match(input, 3, matches), 0);
  EXPECT_EQ(matches[0].rm_so, llvm_regoff_t(1));
  EXPECT_EQ(matches[0].rm_eo, llvm_regoff_t(5));
  EXPECT_EQ(matches[1].rm_so, llvm_regoff_t(3));
  EXPECT_EQ(matches[1].rm_eo, llvm_regoff_t(5));
  EXPECT_EQ(matches[2].rm_so, llvm_regoff_t(-1));
  EXPECT_EQ(matches[2].rm_eo, llvm_regoff_t(-1));
}

TEST(LlvmLibcRegexScalarTest, BackreferencesUseCompleteScalars) {
  Expression re;
  const Char pattern[] = {'^', '\\', '(', 0x4e2d, '.', '\\',
                          ')', '\\', '1', '$',    0};
  ASSERT_EQ(re.compile(pattern), 0);
  const Char good[] = {0x4e2d, 0x1f600, 0x4e2d, 0x1f600, 0};
  const Char bad[] = {0x4e2d, 0x1f600, 0x4e2d, 0x1f601, 0};
  llvm_regmatch_t matches[2];
  ASSERT_EQ(re.match(good, 2, matches), 0);
  EXPECT_EQ(matches[1].rm_so, llvm_regoff_t(0));
  EXPECT_EQ(matches[1].rm_eo, llvm_regoff_t(2));
  EXPECT_EQ(re.match(bad, 2, matches), REG_NOMATCH);
}

TEST(LlvmLibcRegexScalarTest, SentinelsAndBasicEscapeBitDoNotAliasCharacters) {
  const Char scalars[] = {0x80, 0x100, 0x17b, 0x4e2d, 0x1f600, 0x10ffff};
  for (Char scalar : scalars) {
    Expression literal, dot;
    const Char pattern[] = {'^', scalar, '$', 0};
    const Char input[] = {scalar, 0};
    const Char any[] = {'^', '.', '$', 0};
    ASSERT_EQ(literal.compile(pattern), 0);
    ASSERT_EQ(dot.compile(any), 0);
    EXPECT_EQ(literal.match(input, 0, nullptr), 0);
    EXPECT_EQ(dot.match(input, 0, nullptr), 0);
  }
}

TEST(LlvmLibcRegexScalarTest, LargeMachineAndRepeatedCharacterDissection) {
  Expression re;
  Char pattern[100] = {};
  Char input[81] = {};
  for (size_t i = 0; i < 80; ++i) {
    pattern[i] = 0x4e2d;
    input[i] = 0x4e2d;
  }
  ASSERT_EQ(__llvm_libc_bsd_regcomp_chars(&re.value, pattern, 80, 0), 0);
  llvm_regmatch_t match;
  ASSERT_EQ(re.match(input, 1, &match), 0);
  EXPECT_EQ(match.rm_so, llvm_regoff_t(0));
  EXPECT_EQ(match.rm_eo, llvm_regoff_t(80));
  input[79] = 0x2d;
  EXPECT_EQ(re.match(input, 1, &match), REG_NOMATCH);
}

TEST(LlvmLibcRegexScalarTest, ExplicitPartialLocaleBoundary) {
  Expression insensitive, valid;
  const Char literal[] = {0x4e2d, 0};
  EXPECT_EQ(insensitive.compile(literal, REG_ICASE), REG_ECTYPE);
  ASSERT_EQ(valid.compile(literal), 0);
  const Char invalid[] = {0xd800, 0};
  EXPECT_EQ(valid.match(invalid, 0, nullptr), REG_INVARG);
  llvm_regmatch_t range = {0, 2};
  EXPECT_EQ(valid.match(literal, 1, &range, REG_STARTEND), REG_INVARG);
}

TEST(LlvmLibcRegexScalarTest, ScalarRangesAndInversion) {
  Expression positive, negative, newline;
  const Char set[] = {'[',    0x4e2d, '-',    0x4e30, 0x4e31, 0x4e2c,
                      0x4e2d, '-',    0x4e30, ']',    0};
  const Char inverse[] = {'[', '^', 0x4e2d, '-', 0x4e30, ']', 0};
  ASSERT_EQ(positive.compile(set), 0);
  ASSERT_EQ(negative.compile(inverse), 0);
  ASSERT_EQ(newline.compile(inverse, REG_NEWLINE), 0);
  for (Char scalar = 0x4e2b; scalar <= 0x4e32; ++scalar) {
    const Char input[] = {scalar, 0};
    EXPECT_EQ(positive.match(input, 0, nullptr),
              scalar >= 0x4e2c && scalar <= 0x4e31 ? 0 : REG_NOMATCH);
    EXPECT_EQ(negative.match(input, 0, nullptr),
              scalar >= 0x4e2d && scalar <= 0x4e30 ? REG_NOMATCH : 0);
  }
  const Char line[] = {'\n', 0};
  EXPECT_EQ(negative.match(line, 0, nullptr), 0);
  EXPECT_EQ(newline.match(line, 0, nullptr), REG_NOMATCH);
  const Char high[] = {0x10ffff, 0};
  EXPECT_EQ(negative.match(high, 0, nullptr), 0);
}

TEST(LlvmLibcRegexScalarTest, EveryCompilerAllocationHasOwnedCleanup) {
  // Multiple sets, range growth, strip growth, optional mandatory-string
  // optimization and the byte facade all participate in this failure sweep.
  const char *pattern =
      "([acegikmoqsuwy]+)[BDFHJLNPRTVXZ]?[0-9][xyz][a-z][^q][ABC][DEF]"
      "[GHI][JKL][MNO][PQR][STU][VWX]literal";
  size_t before = live, count;
  {
    Expression re;
    attempts = 0;
    ASSERT_EQ(__llvm_libc_bsd_regcomp(&re.value, pattern, REG_EXTENDED), 0);
    count = attempts;
  }
  EXPECT_EQ(live, before);
  ASSERT_GT(count, size_t(16));
  for (size_t at = 0; at < count; ++at) {
    {
      Expression re;
      fail_after = static_cast<int>(at);
      int error = __llvm_libc_bsd_regcomp(&re.value, pattern, REG_EXTENDED);
      fail_after = -1;
      // Shrinking storage and the must-string optimization may fail harmlessly.
      EXPECT_TRUE(error == 0 || error == REG_ESPACE);
    }
    EXPECT_EQ(live, before);
  }
}

TEST(LlvmLibcRegexScalarTest, ExecutionFailureLeavesCaptureStorageUntouched) {
  Expression re;
  const Char pattern[] = {'\\', '(',  '[', 0x4e2d, 0x4e2e, ']',
                          '*',  '\\', ')', '\\',   '1',    0};
  const Char input[] = {0x4e2d, 0x4e2d, 0};
  ASSERT_EQ(re.compile(pattern), 0);
  llvm_regmatch_t matches[2];
  attempts = 0;
  ASSERT_EQ(re.match(input, 2, matches), 0);
  size_t count = attempts, before = live;
  ASSERT_GT(count, size_t(0));
  for (size_t at = 0; at < count; ++at) {
    matches[0] = {77, 88};
    matches[1] = {99, 111};
    fail_after = static_cast<int>(at);
    int error = re.match(input, 2, matches);
    fail_after = -1;
    EXPECT_EQ(error, REG_ESPACE);
    EXPECT_EQ(matches[0].rm_so, llvm_regoff_t(77));
    EXPECT_EQ(matches[1].rm_eo, llvm_regoff_t(111));
    EXPECT_EQ(live, before);
  }
}

TEST(LlvmLibcRegexScalarTest, ByteFacadePreservesAllByteValues) {
  for (unsigned value = 1; value != 256; ++value) {
    Expression re;
    const char pattern[] = {static_cast<char>(value), 0};
    ASSERT_EQ(__llvm_libc_bsd_regcomp(&re.value, pattern, REG_NOSPEC), 0);
    llvm_regmatch_t match;
    ASSERT_EQ(__llvm_libc_bsd_regexec(&re.value, pattern, 1, &match, 0), 0);
    EXPECT_EQ(match.rm_so, llvm_regoff_t(0));
    EXPECT_EQ(match.rm_eo, llvm_regoff_t(1));
  }
}

TEST(LlvmLibcRegexScalarTest, UTF8CompositionUsesOriginalByteOffsets) {
  CharacterRegex re;
  RestartableConverter utf8(true);
  ASSERT_EQ(re.compile("(\xe4\xb8\xad.)(Z)?", REG_EXTENDED, utf8), 0);
  regmatch_t matches[4];
  ASSERT_EQ(re.execute("x\xe4\xb8\xad\xf0\x9f\x98\x80", 4, matches, 0, utf8),
            0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(1));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(8));
  EXPECT_EQ(matches[1].rm_so, regoff_t(1));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(8));
  EXPECT_EQ(matches[2].rm_so, regoff_t(-1));
  EXPECT_EQ(matches[3].rm_so, regoff_t(-1));
  EXPECT_EQ(re.compile("[", 0, utf8), REG_EBRACK);
  EXPECT_EQ(re.execute("\xe4\xb8\xad!", 0, nullptr, 0, utf8), 0);
  ASSERT_EQ(re.compile(".", REG_NOSUB, utf8), 0);
  EXPECT_EQ(re.execute("\xe4\xb8\xad", 99, nullptr, 0, utf8), 0);
}

TEST(LlvmLibcRegexScalarTest, StatefulCapturePrefixesAndControlOnlySuffix) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  CharacterRegex re;
  ASSERT_EQ(re.compile("(\x1b$)A\x0e!!\x0f)(.)", REG_EXTENDED, converter), 0);
  const char *input = "x\x1b$)A\x0e!!\x0fY\x1b$)G";
  regmatch_t matches[3];
  ASSERT_EQ(re.execute(input, 3, matches, 0, converter), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(1));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(10));
  EXPECT_EQ(matches[1].rm_so, regoff_t(1));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(8));
  EXPECT_EQ(matches[2].rm_so, regoff_t(8));
  EXPECT_EQ(matches[2].rm_eo, regoff_t(10));
  ASSERT_EQ(re.compile("()$", REG_EXTENDED, converter), 0);
  ASSERT_EQ(re.execute(input, 2, matches, 0, converter), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(14));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(14));
  EXPECT_EQ(matches[1].rm_so, regoff_t(14));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(14));
  ASSERT_EQ(re.compile("", 0, converter), 0);
  ASSERT_EQ(re.execute("\x1b$)A", 1, matches, 0, converter), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(4));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(4));
}

TEST(LlvmLibcRegexScalarTest, BackreferenceComparesDifferentWireSpellings) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  CharacterRegex re;
  ASSERT_EQ(re.compile("\\(\x1b$)A\x0e!!\x0f\\)\\1", 0, converter), 0);
  const char *input = "\x1b$)A\x0e!!\x0f\x1b$)G\x0e\"!\x0f";
  regmatch_t matches[2];
  ASSERT_EQ(re.execute(input, 2, matches, 0, converter), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(0));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(15));
  EXPECT_EQ(matches[1].rm_so, regoff_t(0));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(7));
}

TEST(LlvmLibcRegexScalarTest, CompositionAllocationRollback) {
  RestartableConverter utf8(true);
  const char *pattern = "(\xe4\xb8\xad[abc]+)(\xf0\x9f\x98\x80)?";
  const char *input = "\xe4\xb8\xad"
                      "abc";
  CharacterRegex re;
  attempts = 0;
  ASSERT_EQ(re.compile(pattern, REG_EXTENDED, utf8), 0);
  size_t compile_count = attempts;
  for (size_t at = 0; at < compile_count; ++at) {
    ASSERT_EQ(re.compile("old", 0, utf8), 0);
    size_t before = live;
    fail_after = static_cast<int>(at);
    int error = re.compile(pattern, REG_EXTENDED, utf8);
    fail_after = -1;
    EXPECT_TRUE(error == 0 || error == REG_ESPACE);
    if (error) {
      EXPECT_EQ(live, before);
      EXPECT_EQ(re.execute("old", 0, nullptr, 0, utf8), 0);
    }
  }
  ASSERT_EQ(re.compile(pattern, REG_EXTENDED, utf8), 0);
  regmatch_t matches[3];
  attempts = 0;
  ASSERT_EQ(re.execute(input, 3, matches, 0, utf8), 0);
  size_t execute_count = attempts, before = live;
  for (size_t at = 0; at < execute_count; ++at) {
    matches[0] = {77, 88};
    fail_after = static_cast<int>(at);
    int error = re.execute(input, 3, matches, 0, utf8);
    fail_after = -1;
    EXPECT_EQ(error, REG_ESPACE);
    EXPECT_EQ(matches[0].rm_so, regoff_t(77));
    EXPECT_EQ(matches[0].rm_eo, regoff_t(88));
    EXPECT_EQ(live, before);
  }
  EXPECT_EQ(re.execute("\xff", 3, matches, 0, utf8), REG_BADPAT);
  EXPECT_EQ(matches[0].rm_so, regoff_t(77));
}

TEST(LlvmLibcRegexScalarTest, BorrowedCTypeClassesAndCaseMapping) {
  __locale_data ctype_c("C", false), ctype_utf8("C.UTF-8", true);
  RestartableConverter utf8(true);
  CharacterRegex re;
  ASSERT_EQ(re.compile("^[[:alpha:]]+$", REG_EXTENDED, utf8, &ctype_c), 0);
  EXPECT_EQ(re.execute("\xc3\xa9", 0, nullptr, 0, utf8), REG_NOMATCH);
  ASSERT_EQ(re.compile("^[[:alpha:]]+$", REG_EXTENDED, utf8, &ctype_utf8), 0);
  EXPECT_EQ(re.execute("\xc3\xa9\xe4\xb8\xad", 0, nullptr, 0, utf8), 0);
  EXPECT_EQ(re.execute("1", 0, nullptr, 0, utf8), REG_NOMATCH);
  ASSERT_EQ(
      re.compile("^[[:lower:]]+$", REG_EXTENDED | REG_ICASE, utf8, &ctype_utf8),
      0);
  EXPECT_EQ(re.execute("\xc3\x89", 0, nullptr, 0, utf8), 0);
  EXPECT_EQ(re.execute("\xe4\xb8\xad", 0, nullptr, 0, utf8), REG_NOMATCH);
  // Uppercase mapping joins Greek normal and final sigma, whose lowercase
  // values alone would not establish their case-insensitive equivalence.
  ASSERT_EQ(re.compile("\xcf\x82", REG_ICASE, utf8, &ctype_utf8), 0);
  EXPECT_EQ(re.execute("\xcf\x83", 0, nullptr, 0, utf8), 0);
  ASSERT_EQ(re.compile("\\(\xc3\xa9\\)\\1", REG_ICASE, utf8, &ctype_utf8), 0);
  regmatch_t matches[2];
  ASSERT_EQ(re.execute("\xc3\xa9\xc3\x89", 2, matches, 0, utf8), 0);
  EXPECT_EQ(matches[0].rm_eo, regoff_t(4));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(2));
}

TEST(LlvmLibcRegexScalarTest, ByteBackreferenceUsesCaseFlag) {
  Expression re;
  ASSERT_EQ(__llvm_libc_bsd_regcomp(&re.value, "\\(a\\)\\1", REG_ICASE), 0);
  llvm_regmatch_t match;
  EXPECT_EQ(__llvm_libc_bsd_regexec(&re.value, "aA", 1, &match, 0), 0);
  EXPECT_EQ(match.rm_eo, llvm_regoff_t(2));
  EXPECT_EQ(__llvm_libc_bsd_regexec(&re.value, "aB", 1, &match, 0),
            REG_NOMATCH);
}

TEST(LlvmLibcRegexScalarTest, CollationAlternativesAndPrimaryEquivalence) {
  LIBC_NAMESPACE::testing::PatternCollationFixture fixture;
  auto collation = fixture.read();
  ASSERT_TRUE(collation.has_value());
  __locale_data ctype("C.UTF-8", true);
  RestartableConverter utf8(true);
  CharacterRegex re;
  auto compile = [&](const char *pattern, int flags = REG_EXTENDED) {
    return re.compile(pattern, flags, utf8, &ctype, &*collation);
  };
  auto match = [&](const char *input) {
    return re.execute(input, 0, nullptr, 0, utf8);
  };
  ASSERT_EQ(compile("^[[.ab.]]+$"), 0);
  EXPECT_EQ(match("abab"), 0);
  EXPECT_EQ(match("a"), REG_NOMATCH);
  ASSERT_EQ(compile("^[[=a=]]$"), 0);
  EXPECT_EQ(match("\xc3\xa4"), 0);
  EXPECT_EQ(match("ba"), 0);
  EXPECT_EQ(match("b"), REG_NOMATCH);
  ASSERT_EQ(compile("^[a-b]$"), 0);
  EXPECT_EQ(match("ab"), 0);
  EXPECT_EQ(match("\xc3\xa4"), 0);
  EXPECT_EQ(match("z"), REG_NOMATCH);
  ASSERT_EQ(compile("^([a-b]{2})([a-b])?$"), 0);
  regmatch_t repeated[3];
  ASSERT_EQ(re.execute("abba", 3, repeated, 0, utf8), 0);
  EXPECT_EQ(repeated[1].rm_eo, regoff_t(4));
  EXPECT_EQ(repeated[2].rm_so, regoff_t(-1));
  ASSERT_EQ(compile("^[[.ab.][.ba.]]+$"), 0);
  EXPECT_EQ(match("abbaab"), 0);
  EXPECT_EQ(match("abbaa"), REG_NOMATCH);
  ASSERT_EQ(compile("^[a[.ab.]]b$"), 0);
  EXPECT_EQ(match("ab"), 0);
  EXPECT_EQ(match("abb"), 0);
  ASSERT_EQ(compile("([a[.ab.]]+)(b)?"), 0);
  regmatch_t captures[3];
  ASSERT_EQ(re.execute("ab", 3, captures, 0, utf8), 0);
  EXPECT_EQ(captures[0].rm_eo, regoff_t(2));
  EXPECT_EQ(captures[1].rm_eo, regoff_t(2));
  EXPECT_EQ(captures[2].rm_so, regoff_t(-1));
  ASSERT_EQ(compile("^\\([[.ab.]]\\)\\1$", 0), 0);
  EXPECT_EQ(match("abab"), 0);
  EXPECT_EQ(match("aba"), REG_NOMATCH);
  ASSERT_EQ(compile("^[[.ab.]]$", REG_EXTENDED | REG_ICASE), 0);
  EXPECT_EQ(match("AB"), 0);
  ASSERT_EQ(compile("^[^a]$", REG_EXTENDED | REG_NEWLINE), 0);
  EXPECT_EQ(match("b"), 0);
  EXPECT_EQ(match("ab"), REG_NOMATCH);
  EXPECT_EQ(match("\n"), REG_NOMATCH);
  EXPECT_EQ(compile("[[.missing.]]"), REG_ECOLLATE);
  EXPECT_EQ(compile("[[=missing=]]"), REG_ECOLLATE);
  EXPECT_EQ(compile("[[:missing:]]"), REG_ECTYPE);
  EXPECT_EQ(compile("[b-a]"), REG_ERANGE);
  EXPECT_EQ(compile("[a-[:alpha:]]"), REG_ERANGE);
  ASSERT_EQ(compile("^[]\\-]$"), 0);
  EXPECT_EQ(match("]"), 0);
  EXPECT_EQ(match("\\"), 0);
  EXPECT_EQ(match("-"), 0);
}

TEST(LlvmLibcRegexScalarTest, LocaleBracketAllocationRollback) {
  LIBC_NAMESPACE::testing::PatternCollationFixture fixture;
  auto collation = fixture.read();
  ASSERT_TRUE(collation.has_value());
  __locale_data ctype("C.UTF-8", true);
  RestartableConverter utf8(true);
  size_t before = live, count;
  auto compile = [&](CharacterRegex &re) {
    return re.compile("^([a[.ab.]]+)([[=a=]]|[a-b])$", REG_EXTENDED | REG_ICASE,
                      utf8, &ctype, &*collation);
  };
  {
    CharacterRegex re;
    attempts = 0;
    ASSERT_EQ(compile(re), 0);
    count = attempts;
    EXPECT_EQ(re.execute("ABA", 0, nullptr, 0, utf8), 0);
  }
  EXPECT_EQ(live, before);
  for (size_t at = 0; at < count; ++at) {
    {
      CharacterRegex re;
      ASSERT_EQ(re.compile("old", 0, utf8), 0);
      size_t old_live = live;
      fail_after = static_cast<int>(at);
      int error = compile(re);
      fail_after = -1;
      EXPECT_TRUE(error == 0 || error == REG_ESPACE);
      if (error) {
        EXPECT_EQ(live, old_live);
        EXPECT_EQ(re.execute("old", 0, nullptr, 0, utf8), 0);
      }
    }
    EXPECT_EQ(live, before);
  }
}

TEST(LlvmLibcRegexScalarTest, EmptyAndUnmatchedBackreferencePaths) {
  CharacterRegex re;
  RestartableConverter utf8(true);
  regmatch_t matches[3];
  ASSERT_EQ(re.compile("(a)?\\1", REG_EXTENDED, utf8), 0);
  EXPECT_EQ(re.execute("", 3, matches, 0, utf8), REG_NOMATCH);
  EXPECT_EQ(re.execute("a", 3, matches, 0, utf8), REG_NOMATCH);
  ASSERT_EQ(re.execute("xaa", 3, matches, 0, utf8), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(1));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(3));
  ASSERT_EQ(re.compile("(a*)\\1", REG_EXTENDED, utf8), 0);
  ASSERT_EQ(re.execute("a", 3, matches, 0, utf8), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(0));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(0));
  EXPECT_EQ(matches[1].rm_so, regoff_t(0));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(0));
  ASSERT_EQ(re.compile("^(a*)\\1b$", REG_EXTENDED, utf8), 0);
  EXPECT_EQ(re.execute("aab", 0, nullptr, 0, utf8), 0);
  EXPECT_EQ(re.execute("ab", 0, nullptr, 0, utf8), REG_NOMATCH);
  EXPECT_EQ(re.execute("b", 0, nullptr, 0, utf8), 0);
}

TEST(LlvmLibcRegexScalarTest, BoundedEmptyBackreferences) {
  const char *patterns[] = {"^\\(\\)\\1\\{255\\}$", "^\\(a*\\)\\1\\{255\\}$"};
  for (const char *pattern : patterns) {
    Expression re;
    ASSERT_EQ(__llvm_libc_bsd_regcomp(&re.value, pattern, 0), 0);
    const Char empty[] = {0};
    llvm_regmatch_t matches[2];
    ASSERT_EQ(re.match(empty, 2, matches), 0);
    EXPECT_EQ(matches[0].rm_so, llvm_regoff_t(0));
    EXPECT_EQ(matches[0].rm_eo, llvm_regoff_t(0));
    EXPECT_EQ(matches[1].rm_so, llvm_regoff_t(0));
    EXPECT_EQ(matches[1].rm_eo, llvm_regoff_t(0));
  }
}

TEST(LlvmLibcRegexScalarTest, UnmatchedNullableReferenceAtInputEnd) {
  Expression re;
  ASSERT_EQ(__llvm_libc_bsd_regcomp(
                &re.value, "^\\(\\(a*\\)b\\)\\{0,1\\}\\2$", 0), 0);
  const Char empty[] = {0};
  llvm_regmatch_t matches[3] = {{77, 88}, {77, 88}, {77, 88}};
  EXPECT_EQ(re.match(empty, 3, matches), REG_NOMATCH);
  EXPECT_EQ(matches[0].rm_so, llvm_regoff_t(77));
  EXPECT_EQ(matches[0].rm_eo, llvm_regoff_t(88));
  EXPECT_EQ(re.match(empty, 0, nullptr), REG_NOMATCH);
}

TEST(LlvmLibcRegexScalarTest, EmptyReferenceLoopsStillMakeProgress) {
  const char *patterns[] = {"^\\(\\)\\1*$", "^\\(\\(\\)\\2*\\)*$",
                            "^\\(a*\\)\\1*$"};
  for (const char *pattern : patterns) {
    Expression re;
    ASSERT_EQ(__llvm_libc_bsd_regcomp(&re.value, pattern, 0), 0);
    const Char empty[] = {0}, nonempty[] = {'x', 0};
    llvm_regmatch_t matches[3];
    EXPECT_EQ(re.match(empty, 3, matches), 0);
    EXPECT_EQ(matches[0].rm_eo, llvm_regoff_t(0));
    EXPECT_EQ(re.match(nonempty, 0, nullptr), REG_NOMATCH);
  }
}

TEST(LlvmLibcRegexScalarTest, AnchorsAndSubexpressionPriority) {
  __locale_data ctype("C.UTF-8", true);
  RestartableConverter utf8(true);
  CharacterRegex re;
  regmatch_t matches[3];
  ASSERT_EQ(re.compile("(a|aa)(a?)", REG_EXTENDED, utf8, &ctype), 0);
  ASSERT_EQ(re.execute("xaa", 3, matches, 0, utf8), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(1));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(3));
  EXPECT_EQ(matches[1].rm_eo, regoff_t(3));
  EXPECT_EQ(matches[2].rm_so, regoff_t(3));
  EXPECT_EQ(matches[2].rm_eo, regoff_t(3));
  ASSERT_EQ(re.compile("^.$", REG_EXTENDED, utf8, &ctype), 0);
  EXPECT_EQ(re.execute("\xc3\xa9", 0, nullptr, REG_NOTBOL, utf8), REG_NOMATCH);
  EXPECT_EQ(re.execute("\xc3\xa9", 0, nullptr, REG_NOTEOL, utf8), REG_NOMATCH);
  ASSERT_EQ(re.compile("^.$", REG_EXTENDED | REG_NEWLINE, utf8, &ctype), 0);
  ASSERT_EQ(
      re.execute("\n\xc3\xa9\n", 1, matches, REG_NOTBOL | REG_NOTEOL, utf8), 0);
  EXPECT_EQ(matches[0].rm_so, regoff_t(1));
  EXPECT_EQ(matches[0].rm_eo, regoff_t(3));
}
