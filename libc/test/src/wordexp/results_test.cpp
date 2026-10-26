//===-- Word expansion result ownership tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/wordexp/results.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::release_word_results;
using LIBC_NAMESPACE::internal::WordResults;

namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0;
bool fail() { return fail_after >= 0 && fail_after-- == 0; }
struct Owned {
  wordexp_t value{};
  ~Owned() { release_word_results(value); }
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

TEST(LlvmLibcWordResultsTest, OffsetAppendAndEmptyWords) {
  Owned result;
  {
    WordResults batch;
    ASSERT_EQ(batch.initialize(3), 0);
    ASSERT_EQ(batch.append("first"), 0);
    ASSERT_EQ(batch.append(""), 0);
    batch.publish(result.value);
  }
  EXPECT_EQ(result.value.we_wordc, size_t(2));
  for (size_t i = 0; i < 3; ++i)
    EXPECT_EQ(result.value.we_wordv[i], static_cast<char *>(nullptr));
  char *first = result.value.we_wordv[3];
  char *empty = result.value.we_wordv[4];
  {
    WordResults batch;
    ASSERT_EQ(batch.initialize(3, &result.value), 0);
    ASSERT_EQ(batch.append("last"), 0);
    EXPECT_EQ(result.value.we_wordc, size_t(2));
    batch.publish(result.value);
  }
  EXPECT_EQ(result.value.we_wordc, size_t(3));
  EXPECT_EQ(result.value.we_offs, size_t(3));
  EXPECT_EQ(result.value.we_wordv[3], first);
  EXPECT_EQ(result.value.we_wordv[4], empty);
  EXPECT_STREQ(result.value.we_wordv[5], "last");
  EXPECT_EQ(result.value.we_wordv[6], static_cast<char *>(nullptr));
}

TEST(LlvmLibcWordResultsTest, RollbackLeavesPreviousIdentity) {
  Owned result;
  WordResults initial;
  ASSERT_EQ(initial.initialize(0), 0);
  ASSERT_EQ(initial.append("previous"), 0);
  initial.publish(result.value);
  auto **previous = result.value.we_wordv;
  size_t before = live;
  {
    WordResults batch;
    ASSERT_EQ(batch.initialize(0, &result.value), 0);
    ASSERT_EQ(batch.append("discarded"), 0);
  }
  EXPECT_EQ(live, before);
  EXPECT_EQ(result.value.we_wordv, previous);
  EXPECT_EQ(result.value.we_wordc, size_t(1));
  EXPECT_STREQ(previous[0], "previous");
}

TEST(LlvmLibcWordResultsTest, EveryAllocationFailureHasAReleasablePrefix) {
  const bool modes[] = {false, true};
  for (bool publish : modes) {
    bool complete = false;
    for (int failure = 0; !complete && failure < 40; ++failure) {
      size_t before = live;
      {
        Owned result;
        WordResults initial;
        ASSERT_EQ(initial.initialize(2), 0);
        ASSERT_EQ(initial.append("previous"), 0);
        initial.publish(result.value);
        auto **old = result.value.we_wordv;
        char *old_word = old[2];
        {
          WordResults batch;
          fail_after = failure;
          int error = batch.initialize(2, &result.value);
          bool initialized = error == 0;
          for (unsigned i = 0; i < 12 && !error; ++i)
            error = batch.append("next");
          fail_after = -1;
          complete = error == 0;
          EXPECT_EQ(result.value.we_wordv, old);
          EXPECT_EQ(result.value.we_wordc, size_t(1));
          if (initialized && publish) {
            size_t count = batch.size();
            batch.publish(result.value);
            EXPECT_EQ(result.value.we_wordc, count);
            EXPECT_EQ(result.value.we_wordv[2], old_word);
            EXPECT_EQ(result.value.we_wordv[2 + count],
                      static_cast<char *>(nullptr));
          }
        }
        EXPECT_STREQ(result.value.we_wordv[2], "previous");
      }
      EXPECT_EQ(live, before);
    }
    EXPECT_TRUE(complete);
  }
}

TEST(LlvmLibcWordResultsTest, EmptyResultsAndOverflow) {
  Owned result;
  WordResults empty;
  ASSERT_EQ(empty.initialize(0), 0);
  empty.publish(result.value);
  EXPECT_EQ(result.value.we_wordc, size_t(0));
  EXPECT_EQ(result.value.we_wordv[0], static_cast<char *>(nullptr));
  WordResults huge;
  EXPECT_EQ(huge.initialize(size_t(-1)), ENOMEM);
  WordResults mismatch;
  EXPECT_EQ(mismatch.initialize(1, &result.value), EINVAL);
}
