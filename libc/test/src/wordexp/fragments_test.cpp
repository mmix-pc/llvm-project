//===-- Word fragment allocation tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/wordexp/expand.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::cpp::span;
using LIBC_NAMESPACE::cpp::string_view;

namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0;
bool fail() { return fail_after >= 0 && fail_after-- == 0; }
ErrorOr<WordVariable> lookup(void *, string_view name) {
  return WordVariable{true, name == "x" ? "abcdef" : "value"};
}
int accept_word(void *, span<const ExpansionPart>) { return 0; }
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

TEST(LlvmLibcWordFragmentsTest, OwnsAndPreservesDistinctExpansionOrigins) {
  size_t before = live;
  {
    WordFragments result;
    char text[] = "first";
    ASSERT_EQ(result.append(text, true, false), 0);
    text[0] = 'X';
    ASSERT_EQ(result.append("second", true, false), 0);
    ASSERT_EQ(result.append("literal", false, false), 0);
    ASSERT_EQ(result.append("-suffix", false, false), 0);
    ASSERT_EQ(result.append("", false, true), 0);
    ASSERT_EQ(result.parts().size(), size_t(4));
    EXPECT_TRUE(result.parts()[0].bytes == "first");
    EXPECT_TRUE(result.parts()[1].bytes == "second");
    EXPECT_TRUE(result.parts()[2].bytes == "literal-suffix");
    EXPECT_TRUE(result.parts()[3].quoted);
    result.clear();
    EXPECT_EQ(result.parts().size(), size_t(0));
    ASSERT_EQ(result.append("reused", true, false), 0);
    EXPECT_TRUE(result.parts()[0].bytes == "reused");
  }
  EXPECT_EQ(live, before);
}

TEST(LlvmLibcWordFragmentsTest, FailedGrowthPreservesPriorFragments) {
  size_t before = live;
  bool completed = false;
  for (int failure = 0; failure < 100 && !completed; ++failure) {
    {
      WordFragments result;
      ASSERT_EQ(result.append("retained", true, false), 0);
      fail_after = failure;
      int error = 0;
      for (unsigned i = 0; i < 20 && !error; ++i)
        error = result.append("another origin", true, false);
      for (unsigned i = 0; i < 200 && !error; ++i)
        error = result.append("x", false, true);
      fail_after = -1;
      EXPECT_TRUE(result.parts()[0].bytes == "retained");
      completed = error == 0;
      if (error)
        EXPECT_EQ(error, ENOMEM);
    }
    EXPECT_EQ(live, before);
  }
  EXPECT_TRUE(completed);
}

TEST(LlvmLibcWordFragmentsTest, ExpansionFailureReleasesEveryOwnedAllocation) {
  uint32_t source[512];
  size_t size = 0;
  for (unsigned i = 0; i < 200; ++i)
    source[size++] = 'a';
  for (char c : string_view("${x%%*f} '${y}' ${y:-fallback} $((2+3))"))
    source[size++] = static_cast<unsigned char>(c);
  WordExpansionServices services;
  services.lookup = lookup;
  bool completed = false;
  size_t before = live;
  for (int failure = 0; failure < 512 && !completed; ++failure) {
    fail_after = failure;
    auto result = expand_word_fragments(
        {source, size}, true, false, RestartableConverter(false), nullptr,
        nullptr, services, {nullptr, accept_word});
    fail_after = -1;
    completed = bool(result);
    if (!completed) {
      EXPECT_TRUE(result.error == WordExpansionError::SYSTEM);
      EXPECT_EQ(result.system_error, ENOMEM);
    }
    EXPECT_EQ(live, before);
  }
  EXPECT_TRUE(completed);
}

TEST(LlvmLibcWordFragmentsTest, OwnedSourceExpansionFailureReleasesAllStorage) {
  char text[512];
  size_t size = 0;
  for (unsigned i = 0; i < 200; ++i)
    text[size++] = 'a';
  for (char c : string_view("${absent:-${x%%*f} '$y' $'\\x41\\n'} $((2+3))"))
    text[size++] = c;
  WordExpansionServices services;
  services.lookup = [](void *p, string_view name) -> ErrorOr<WordVariable> {
    return name == "absent" ? WordVariable{false, {}} : *lookup(p, name);
  };
  bool completed = false;
  size_t before = live;
  for (int failure = 0; failure < 512 && !completed; ++failure) {
    int error;
    {
      WordSource source;
      fail_after = failure;
      error = source.assign({text, size}, RestartableConverter(false));
      if (!error) {
        auto result = expand_word_fragments(
            source, true, false, RestartableConverter(false), nullptr,
            nullptr, services, {nullptr, accept_word});
        error = result ? 0 : result.system_error;
        fail_after = -1;
        EXPECT_TRUE(result || result.error == WordExpansionError::SYSTEM);
      }
      fail_after = -1;
      completed = error == 0;
      if (error)
        EXPECT_EQ(error, ENOMEM);
    }
    EXPECT_EQ(live, before);
  }
  EXPECT_TRUE(completed);
}
