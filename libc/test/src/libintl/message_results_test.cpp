//===-- Stable message result tests ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/alloc-checker.h"
#include "src/libintl/message_results.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;
using internal::MessageResults;

namespace {
char *copy(cpp::string_view text) {
  AllocChecker ac;
  char *result = new (ac) char[text.size() + 1];
  if (!ac)
    return nullptr;
  for (size_t i = 0; i != text.size(); ++i)
    result[i] = text[i];
  result[text.size()] = 0;
  return result;
}
struct Results : MessageResults {
  ~Results() { clear(); }
};
} // namespace

TEST(LlvmLibcMessageResultsTest, RepeatedMessagesPreserveIdentity) {
  Results results;
  auto first = results.insert(copy("hello"), 5);
  ASSERT_TRUE(first.has_value());
  for (unsigned i = 0; i != 32; ++i) {
    auto repeat = results.insert(copy("hello"), 5);
    ASSERT_TRUE(repeat.has_value());
    EXPECT_EQ(*repeat, *first);
  }
  auto different = results.insert(copy("world"), 5);
  ASSERT_TRUE(different.has_value());
  EXPECT_NE(*different, *first);
  EXPECT_STREQ(*first, "hello");
}

TEST(LlvmLibcMessageResultsTest, CountedBytesAndHashCollisions) {
  Results results;
  const cpp::string_view texts[] = {{"\0\0\0A", 4}, {"\0\0\0B", 4}, "a",
                                    "aa",           "b@",           ""};
  // aa and b@ share the same hash; embedded zero is not a string terminator.
  char *saved[6];
  for (unsigned i = 0; i != 6; ++i) {
    auto value = results.insert(copy(texts[i]), texts[i].size());
    ASSERT_TRUE(value.has_value());
    saved[i] = *value;
    for (unsigned j = 0; j != i; ++j)
      EXPECT_NE(saved[i], saved[j]);
  }
  for (unsigned i = 0; i != 6; ++i) {
    auto value = results.insert(copy(texts[i]), texts[i].size());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, saved[i]);
    EXPECT_TRUE(cpp::string_view(saved[i], texts[i].size()) == texts[i]);
  }
}

#ifdef MESSAGE_RESULTS_ALLOCATION_TEST
namespace {
bool tracking = false;
unsigned calls = 0, failure = 0, freed = 0;
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == failure)
    return nullptr;
  return __real_malloc(size);
}
extern "C" void __wrap_free(void *value) {
  if (tracking && value)
    ++freed;
  __real_free(value);
}
TEST(LlvmLibcMessageResultsTest, FailedPublicationConsumesInputNotOldResults) {
  Results results;
  auto first = results.insert(copy("hello"), 5);
  ASSERT_TRUE(first.has_value());
  char *incoming = copy("world");
  ASSERT_NE(incoming, nullptr);
  calls = freed = 0;
  failure = 1;
  tracking = true;
  auto failed = results.insert(incoming, 5);
  tracking = false;
  EXPECT_EQ(failed.error(), ENOMEM);
  EXPECT_EQ(freed, 1U);
  EXPECT_STREQ(*first, "hello");
  incoming = copy("hello");
  ASSERT_NE(incoming, nullptr);
  calls = freed = 0;
  tracking = true;
  auto repeat = results.insert(incoming, 5);
  tracking = false;
  ASSERT_TRUE(repeat.has_value());
  EXPECT_EQ(*repeat, *first);
  EXPECT_EQ(calls, 0U);
  EXPECT_EQ(freed, 1U);
}
#endif
