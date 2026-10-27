//===-- Locale pathname matcher tests -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fnmatch_macros.h"
#include "src/fnmatch/matcher.h"
#ifdef LIBC_FULL_BUILD
#include "src/locale/locale.h"
#endif
#include "test/UnitTest/Test.h"
#include "test/src/locale/PatternCollationFixture.h"

using namespace LIBC_NAMESPACE;
using namespace LIBC_NAMESPACE::internal;

namespace {
auto match(const char *pattern, const char *input, int flags = 0,
           const CollationData *collation = nullptr) {
  return match_path_pattern(pattern, input, flags, RestartableConverter(true),
                            nullptr, collation);
}

using Collation = LIBC_NAMESPACE::testing::PatternCollationFixture;
} // namespace

TEST(LlvmLibcPathMatcherTest, UnicodeCharactersNotBytes) {
  EXPECT_TRUE(match("?", "\xc3\xa4").value());
  EXPECT_FALSE(match("??", "\xc3\xa4").value());
  EXPECT_TRUE(match("a?c", "a\xe4\xb8\xad"
                           "c")
                  .value());
  EXPECT_TRUE(match("*[\xc3\xa4]", "word\xc3\xa4").value());
  EXPECT_TRUE(match("[!\xc3\xa4]", "\xe4\xb8\xad").value());
  EXPECT_TRUE(match("[\xc3\xa4-\xc3\xb6]", "\xc3\xa5").value());
  EXPECT_TRUE(match("*?/?", "ab\xc3\xa4/\xe4\xb8\xad", FNM_PATHNAME).value());
  EXPECT_FALSE(
      match("*?/?", "ab\xc3\xa4/.x", FNM_PATHNAME | FNM_PERIOD).value());
}

TEST(LlvmLibcPathMatcherTest, InvalidEncodingAndIndependentCalls) {
  const char *bad[] = {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80",
                       "\xe4\xb8", "\xff"};
  for (const char *text : bad) {
    EXPECT_EQ(match("*", text).error(), EILSEQ);
    EXPECT_EQ(match(text, "text").error(), EILSEQ);
    EXPECT_TRUE(match("?", "\xc3\xa4").value());
  }
  EXPECT_TRUE(match_path_pattern("?", "\xff", 0, RestartableConverter(false),
                                 nullptr, nullptr)
                  .value());
}

TEST(LlvmLibcPathMatcherTest, CollatingElementsEquivalenceAndRanges) {
  Collation fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  auto selected = [&](const char *p, const char *s) {
    return match(p, s, 0, &*data).value();
  };
  EXPECT_TRUE(selected("[[.ab.]]", "ab"));
  EXPECT_FALSE(selected("[ab]", "ab"));
  EXPECT_TRUE(selected("[[=a=]]", "\xc3\xa4"));
  EXPECT_TRUE(selected("[[=\xc3\xa4=]]", "a"));
  EXPECT_FALSE(selected("[[=a=]]", "b"));
  EXPECT_TRUE(selected("[a-b]", "\xc3\xa4"));
  EXPECT_TRUE(selected("[a-b]", "ab"));
  EXPECT_FALSE(selected("[[.missing.]]", "m"));
  // A bracket can consume either one or two characters. Neither greedy-only
  // choice is sufficient; each suffix must select the appropriate alternative.
  EXPECT_TRUE(selected("[a[.ab.]]b", "ab"));
  EXPECT_TRUE(selected("[a[.ab.]]c", "abc"));
  EXPECT_TRUE(selected("*[a[.ab.]]b", "aab"));
  EXPECT_TRUE(selected("*[a[.ab.]]c", "aabc"));
  // The chosen unspecified negative-list policy consumes one character only.
  EXPECT_TRUE(selected("[!a]", "b"));
  EXPECT_FALSE(selected("[!a]", "ab"));
}

TEST(LlvmLibcPathMatcherTest, CollatingElementCaseCounterparts) {
  Collation fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  const int flags[] = {FNM_CASEFOLD, FNM_IGNORECASE};
  const char *counterparts[] = {"ab", "aB", "Ab", "AB"};
  const char *different_strings[] = {"a", "b", "ac", "aC", "CB", "ABA"};
  for (int flag : flags) {
    for (const char *counterpart : counterparts) {
      auto result = match("[[.ab.]]", counterpart, flag, &*data);
      ASSERT_TRUE(result.has_value());
      EXPECT_TRUE(*result);
      EXPECT_TRUE(match("[a-b]", counterpart, flag, &*data).value());
    }
    for (const char *different : different_strings) {
      auto result = match("[[.ab.]]", different, flag, &*data);
      ASSERT_TRUE(result.has_value());
      EXPECT_FALSE(*result);
    }
  }
}

TEST(LlvmLibcPathMatcherTest, CaseSetsPreserveOriginalMembers) {
  EXPECT_FALSE(match("[[:upper:]]", "a").value());
  EXPECT_FALSE(match("[[:lower:]]", "A").value());
  const int flags[] = {FNM_CASEFOLD, FNM_IGNORECASE};
  for (int flag : flags) {
    EXPECT_TRUE(match("[[:upper:]]", "a", flag).value());
    EXPECT_TRUE(match("[[:lower:]]", "A", flag).value());
    EXPECT_FALSE(match("[![:upper:]]", "a", flag).value());
    EXPECT_TRUE(match("[a-z]", "A", flag).value());
    EXPECT_TRUE(match("[A-Z]", "a", flag).value());
    // Folding both endpoints would reverse this otherwise valid interval.
    EXPECT_TRUE(match("[Z-a]", "_", flag).value());
    EXPECT_TRUE(match("[Z-a]", "Z", flag).value());
    EXPECT_FALSE(match("[Z-a]", "b", flag).value());
    Collation fixture;
    auto data = fixture.read();
    ASSERT_TRUE(data.has_value());
    EXPECT_TRUE(match("[[=a=]]", "BA", flag, &*data).value());
    EXPECT_TRUE(match("[[=a=]]", "bA", flag, &*data).value());
    EXPECT_FALSE(match("[[=a=]]", "AB", flag, &*data).value());
  }
}

#ifdef LIBC_FULL_BUILD
TEST(LlvmLibcPathMatcherTest, NonBijectiveCaseMapping) {
  __locale_data category("C.UTF-8", true);
  auto selected = [&](const char *pattern, const char *input) {
    return match_path_pattern(pattern, input, FNM_CASEFOLD,
                              RestartableConverter(true), &category, nullptr)
        .value();
  };
  EXPECT_TRUE(selected("\xcf\x82", "\xce\xa3"));
  EXPECT_TRUE(selected("\xcf\x82", "\xcf\x83"));
  EXPECT_TRUE(selected("[\xcf\x82]", "\xce\xa3"));
  EXPECT_TRUE(selected("[[=\xcf\x82=]]", "\xce\xa3"));
  EXPECT_TRUE(selected("[\xcf\x82-\xcf\x82]", "\xce\xa3"));
  EXPECT_FALSE(selected("[!\xcf\x82]", "\xce\xa3"));
  EXPECT_FALSE(selected("\xcf\x82", "\xce\xb1"));
  // U+00B5 lies inside this interval and maps to U+039C, but the reverse
  // lowercase mapping is U+03BC, outside the original interval.
  EXPECT_TRUE(selected("[m-\xc2\xb6]", "\xce\x9c"));
  EXPECT_FALSE(selected("[m-\xc2\xb4]", "\xce\x9c"));
}
#endif

#ifdef PATH_MATCHER_ALLOCATION_TEST
namespace {
bool tracking = false;
unsigned calls = 0, fail_call = 0, live = 0;
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *result = __real_malloc(size);
  if (tracking && result)
    ++live;
  return result;
}
extern "C" void __wrap_free(void *value) {
  if (tracking && value)
    --live;
  __real_free(value);
}
TEST(LlvmLibcPathMatcherTest, AllocationFailureReleasesPreparedState) {
  for (unsigned i = 1; i <= 5; ++i) {
    calls = 0;
    fail_call = i;
    tracking = true;
    auto result = match("a*[b]", "ab");
    tracking = false;
    EXPECT_EQ(result.error(), ENOMEM);
    EXPECT_EQ(live, 0U);
  }
}
#endif
