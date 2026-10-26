//===-- Word expansion pathname composition tests -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/wordexp/paths.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::cpp::span;
using LIBC_NAMESPACE::cpp::string_view;

namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0;
bool fail() { return fail_after >= 0 && fail_after-- == 0; }
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

namespace {
struct Published {
  wordexp_t value{};
  ~Published() { release_word_results(value); }
};
void check(span<const ExpansionPart> parts, span<const string_view> expected) {
  WordResults batch;
  ASSERT_EQ(batch.initialize(2), 0);
  ASSERT_EQ(append_word_paths(parts, DEFAULT_FIELD_DELIMITERS,
                              RestartableConverter(false), batch),
            0);
  Published out;
  batch.publish(out.value);
  ASSERT_EQ(out.value.we_wordc, expected.size());
  EXPECT_EQ(out.value.we_wordv[0], static_cast<char *>(nullptr));
  EXPECT_EQ(out.value.we_wordv[1], static_cast<char *>(nullptr));
  EXPECT_EQ(out.value.we_wordv[2 + expected.size()],
            static_cast<char *>(nullptr));
  for (size_t i = 0; i < expected.size(); ++i)
    EXPECT_TRUE(string_view(out.value.we_wordv[2 + i]) == expected[i]);
}
} // namespace

TEST(LlvmLibcWordPathsTest, SplittingPrecedesGlobAndQuotesProtectSyntax) {
  ExpansionPart parts[] = {{"paths-data/[ab].txt paths-data/*", true, false}};
  string_view expected[] = {"paths-data/a.txt",     "paths-data/b.txt",
                            "paths-data/a.txt",     "paths-data/b.txt",
                            "paths-data/back\\one", "paths-data/star*"};
  check(parts, expected);
  ExpansionPart quoted[] = {{"paths-data/[ab].txt paths-data/*", true, true}};
  string_view literal[] = {"paths-data/[ab].txt paths-data/*"};
  check(quoted, literal);
  ExpansionPart mixed[] = {{"paths-data/", false, false},
                           {"star*", false, true},
                           {"*", true, false}};
  string_view star[] = {"paths-data/star*"};
  check(mixed, star);
}

TEST(LlvmLibcWordPathsTest, ExpansionBackslashesAreLiteral) {
  ExpansionPart parts[] = {{"paths-data/back\\*", true, false}};
  string_view expected[] = {"paths-data/back\\one"};
  check(parts, expected);
  ExpansionPart unmatched[] = {{"no-such/\\*", true, false},
                               {"[", false, true}};
  string_view original[] = {"no-such/\\*["};
  check(unmatched, original);
}

TEST(LlvmLibcWordPathsTest, EmptyAndArbitraryLiteralBytes) {
  ExpansionPart quoted[] = {{"", false, true}};
  string_view empty[] = {""};
  check(quoted, empty);
  ExpansionPart removed[] = {{"", true, false}};
  check(removed, {});
  WordResults batch;
  ASSERT_EQ(batch.initialize(0), 0);
  ExpansionPart raw[] = {{"\xff", true, false}};
  ASSERT_EQ(append_word_paths(raw, {}, RestartableConverter(true), batch), 0);
  Published out;
  batch.publish(out.value);
  ASSERT_EQ(out.value.we_wordc, size_t(1));
  EXPECT_TRUE(string_view(out.value.we_wordv[0]) == "\xff");
}

TEST(LlvmLibcWordPathsTest, QuotedBracketContentsRemainLiteral) {
  ExpansionPart parts[] = {{"paths-data/[", false, false},
                           {"a", false, true},
                           {"b].txt", false, false}};
  string_view expected[] = {"paths-data/a.txt", "paths-data/b.txt"};
  check(parts, expected);
}

TEST(LlvmLibcWordPathsTest, FailureReleasesAllTemporaryOwnership) {
  size_t before = live;
  bool completed = false;
  ExpansionPart parts[] = {{"paths-data/*", true, false}};
  for (int failure = 0; failure < 256 && !completed; ++failure) {
    {
      WordResults batch;
      ASSERT_EQ(batch.initialize(0), 0);
      ASSERT_EQ(batch.append("previous"), 0);
      fail_after = failure;
      int error = append_word_paths(parts, DEFAULT_FIELD_DELIMITERS,
                                    RestartableConverter(false), batch);
      fail_after = -1;
      completed = error == 0;
      if (error)
        EXPECT_EQ(error, ENOMEM);
      EXPECT_GE(batch.size(), size_t(1));
      Published out;
      batch.publish(out.value);
      EXPECT_STREQ(out.value.we_wordv[0], "previous");
    }
    EXPECT_EQ(live, before);
  }
  EXPECT_TRUE(completed);
}
