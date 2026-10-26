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
