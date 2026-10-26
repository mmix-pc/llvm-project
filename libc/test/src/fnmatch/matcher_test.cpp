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

using namespace LIBC_NAMESPACE;
using namespace LIBC_NAMESPACE::internal;

namespace {
auto match(const char *pattern, const char *input, int flags = 0,
           const CollationData *collation = nullptr) {
  return match_path_pattern(pattern, input, flags, RestartableConverter(true),
                            nullptr, collation);
}

struct Collation {
  uint8_t bytes[1024]{};
  static void put(uint8_t *where, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      where[i] = uint8_t(value >> (24 - 8 * i));
  }
  auto read() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = magic[i];
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    const uint32_t records[][15] = {
        {4, 8, 1, 1, 1, 1, 1},
        {1, 'a', 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        {2, 'a', 'b', 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1},
        {1, 'b', 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1},
        {1, 0xe4, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1}};
    const unsigned lengths[] = {7, 14, 15, 14, 14};
    size_t size = 80;
    for (unsigned i = 0; i != 5; ++i) {
      uint8_t *entry = bytes + 130 + i * 16;
      put(entry, i ? 0x1000 + i - 1 : 1);
      put(entry + 4, 2);
      put(entry + 8, uint32_t(size));
      put(entry + 12, lengths[i] * 4);
      for (unsigned j = 0; j != lengths[i]; ++j)
        put(bytes + 130 + size + j * 4, records[i][j]);
      size += lengths[i] * 4;
    }
    put(bytes + 16, uint32_t(130 + size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *entry = bytes + 32 + i * 16;
      put(entry, i);
      put(entry + 4, uint32_t(i <= 3 ? 130 : 130 + size));
      put(entry + 8, uint32_t(i == 3 ? size : 0));
      put(entry + 12, i == 3 ? 5 : 0);
    }
    auto view = LocaleDataView::parse({bytes, 130 + size});
    if (!view)
      return ErrorOr<CollationData>(Error(view.error()));
    return CollationData::read(*view);
  }
};
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
