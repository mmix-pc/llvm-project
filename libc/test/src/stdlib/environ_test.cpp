//===-- Environment replacement, ownership and allocation tests -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "src/stdlib/environ_internal.h"
#include "test/UnitTest/Test.h"
#ifdef LIBC_COPT_SUPPORT_ENVIRON
#include "src/unistd/environ.h"
#endif
#ifdef LIBC_TEST_ENVIRON_MODIFIERS
#include "src/__support/libc_errno.h"
#include "src/stdlib/putenv.h"
#include "src/stdlib/setenv.h"
#include "src/stdlib/unsetenv.h"
#endif

namespace LIBC_NAMESPACE_DECL {
#ifndef LIBC_FULL_BUILD
AppProperties app;
#endif
#ifdef LIBC_TEST_ENVIRON_STUB
// Test the public-environ path without replacing the host libc's environ.
char **environ;
#endif
} // namespace LIBC_NAMESPACE_DECL

namespace {
bool tracking = false;
unsigned attempts = 0, fail_at = 0, invalid_releases = 0;
unsigned records = 0, live = 0;
uintptr_t allocations[512]{};

struct TrackAllocations {
  TrackAllocations() { tracking = true; }
  ~TrackAllocations() { tracking = false; }
};

char **current_array() {
#ifdef LIBC_COPT_SUPPORT_ENVIRON
  return LIBC_NAMESPACE::environ;
#else
  return reinterpret_cast<char **>(LIBC_NAMESPACE::app.env_ptr);
#endif
}

void replace_array(char **array) {
#ifdef LIBC_COPT_SUPPORT_ENVIRON
  // Deliberately do not update app.env_ptr: an application cannot do so.
  LIBC_NAMESPACE::environ = array;
#else
  LIBC_NAMESPACE::app.env_ptr = reinterpret_cast<uintptr_t *>(array);
#endif
}

unsigned allocation_id(void *ptr) {
  for (unsigned i = 0; i < records; ++i)
    if (allocations[i] == reinterpret_cast<uintptr_t>(ptr))
      return i;
  __builtin_trap();
}
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++attempts == fail_at)
    return nullptr;
  void *ptr = __real_malloc(size);
  if (tracking && ptr) {
    if (records == 512)
      __builtin_trap();
    allocations[records++] = reinterpret_cast<uintptr_t>(ptr);
    ++live;
  }
  return ptr;
}

extern "C" void __wrap_free(void *ptr) {
  if (tracking && ptr) {
    for (unsigned i = 0; i < records; ++i) {
      if (allocations[i] == reinterpret_cast<uintptr_t>(ptr)) {
        allocations[i] = 0;
        --live;
        __real_free(ptr);
        return;
      }
    }
    // Report borrowed frees and double frees without freeing stack memory.
    ++invalid_releases;
    return;
  }
  __real_free(ptr);
}

class LlvmLibcEnvironmentTest : public LIBC_NAMESPACE::testing::Test {
protected:
  using Manager = LIBC_NAMESPACE::internal::EnvironmentManager;
  Manager &env = Manager::get_instance();

  void SetUp() override {
    env.clear();
    attempts = fail_at = invalid_releases = records = live = 0;
  }

  void clear() {
    TrackAllocations track;
    env.clear();
  }

  void TearDown() override {
    // Local borrowed arrays may already be out of scope. clear must not read
    // them, nor restore an array that a successful mutation has freed.
    clear();
    EXPECT_EQ(live, 0U);
    EXPECT_EQ(invalid_releases, 0U);
  }

  int set(const char *name, const char *value, bool overwrite = true) {
    TrackAllocations track;
    return env.set(name, value, overwrite);
  }

  int put(char *entry) {
    TrackAllocations track;
    return env.put(entry);
  }

  int unset(const char *name) {
    TrackAllocations track;
    return env.unset(name);
  }

  void fail_on(unsigned attempt) {
    attempts = 0;
    fail_at = attempt;
  }
};

TEST_F(LlvmLibcEnvironmentTest, ReplacementIsAuthoritative) {
  char first[] = "FIRST=one", last[] = "LAST=two";
  char *external[] = {first, last, nullptr};
  replace_array(external);
  EXPECT_EQ(env.get("LAST"), last + 5);
  EXPECT_EQ(env.begin(), external);
  EXPECT_EQ(env.end(), external + 2);
  EXPECT_EQ(env.size(), size_t(2));
  ASSERT_EQ(set("OWNED", "old"), 0);

  char replacement[] = "REPLACEMENT=yes";
  char *shorter[] = {replacement, nullptr};
  replace_array(shorter);
  EXPECT_EQ(env.get("REPLACEMENT"), replacement + 12);
  EXPECT_EQ(env.get("OWNED"), nullptr);
  EXPECT_EQ(env.size(), size_t(1));
  ASSERT_EQ(set("REPLACEMENT", "ignored", false), 0);
  EXPECT_EQ(current_array(), shorter);
  ASSERT_EQ(set("NEW", "value"), 0);
  EXPECT_EQ(env.get("REPLACEMENT"), replacement + 12);
  EXPECT_EQ(env.get("OWNED"), nullptr);
  EXPECT_STREQ(env.get("NEW"), "value");

  replace_array(nullptr);
  EXPECT_EQ(env.get("NEW"), nullptr);
  EXPECT_EQ(env.begin(), nullptr);
  EXPECT_EQ(env.end(), nullptr);
  EXPECT_EQ(env.size(), size_t(0));
  ASSERT_EQ(set("AFTER_NULL", "value"), 0);
  EXPECT_EQ(env.size(), size_t(1));
  char *empty[] = {nullptr};
  replace_array(empty);
  EXPECT_EQ(env.size(), size_t(0));
  EXPECT_EQ(env.end(), empty);
  ASSERT_EQ(put(first), 0);
  EXPECT_EQ(env.get("FIRST"), first + 6);
}

TEST_F(LlvmLibcEnvironmentTest, BorrowedStringsAndInPlaceChanges) {
  char entry[] = "A=one", other[] = "B=two", last[] = "C=three";
  char *external[] = {entry, other, last, nullptr};
  replace_array(external);
  EXPECT_EQ(env.size(), size_t(3));
  external[1] = nullptr;
  EXPECT_EQ(env.get("B"), nullptr);
  EXPECT_EQ(env.size(), size_t(1));
  external[1] = other;
  EXPECT_EQ(env.get("C"), last + 2);
  ASSERT_EQ(put(entry), 0);
  entry[2] = 'O';
  EXPECT_EQ(env.get("A"), entry + 2);
  EXPECT_STREQ(env.get("A"), "One");
  ASSERT_EQ(set("B", "owned"), 0);
  // Public slots do not carry ownership. Replacing an owned entry with a
  // borrowed one must neither leak the old string nor free the replacement.
  current_array()[1] = other;
  ASSERT_EQ(unset("B"), 0);
  ASSERT_EQ(unset("A"), 0);
  EXPECT_STREQ(entry, "A=One");
  EXPECT_STREQ(other, "B=two");
  EXPECT_EQ(live, 2U); // Only the managed arrays remain.
  clear();
  EXPECT_STREQ(last, "C=three");
}

TEST_F(LlvmLibcEnvironmentTest, OwnedAliasesSurviveReplacementAndDuplicates) {
  ASSERT_EQ(set("KEEP", "value"), 0);
  char *kept = current_array()[0];
  unsigned kept_id = allocation_id(kept);
  ASSERT_EQ(set("DROP", "obsolete"), 0);
  unsigned dropped_id = allocation_id(current_array()[1]);
  unsigned array_id = allocation_id(current_array());
  char borrowed[] = "BORROWED=value";
  char *replacement[] = {kept, kept, borrowed, nullptr};
  replace_array(replacement);
  ASSERT_EQ(set("NEW", env.get("KEEP")), 0);
  EXPECT_EQ(allocations[array_id], uintptr_t(0));
  EXPECT_EQ(allocations[dropped_id], uintptr_t(0));
  EXPECT_NE(allocations[kept_id], uintptr_t(0));
  EXPECT_EQ(current_array()[0], kept);
  EXPECT_EQ(current_array()[1], kept);
  ASSERT_EQ(set("KEEP", "replacement"), 0);
  EXPECT_NE(allocations[kept_id], uintptr_t(0));
  ASSERT_EQ(unset("KEEP"), 0);
  EXPECT_EQ(allocations[kept_id], uintptr_t(0));
  EXPECT_EQ(env.get("KEEP"), nullptr);
  EXPECT_STREQ(env.get("NEW"), "value");
  clear();
  EXPECT_EQ(live, 0U);
  EXPECT_STREQ(borrowed, "BORROWED=value");
}

TEST_F(LlvmLibcEnvironmentTest, PutReusesOwnedStringAndSuffix) {
  ASSERT_EQ(set("A", "B=value"), 0);
  char *entry = current_array()[0];
  unsigned id = allocation_id(entry);
  ASSERT_EQ(put(entry), 0);
  EXPECT_STREQ(env.get("A"), "B=value");
  ASSERT_EQ(put(env.get("A")), 0);
  ASSERT_EQ(unset("A"), 0);
  EXPECT_NE(allocations[id], uintptr_t(0));
  EXPECT_STREQ(env.get("B"), "value");
  ASSERT_EQ(unset("B"), 0);
  EXPECT_EQ(allocations[id], uintptr_t(0));
}

TEST_F(LlvmLibcEnvironmentTest, ReplacementCanPointInsideManagedArray) {
  ASSERT_EQ(set("DROP", "first"), 0);
  ASSERT_EQ(set("KEEP", "second"), 0);
  unsigned array_id = allocation_id(current_array());
  replace_array(current_array() + 1);
  EXPECT_EQ(env.size(), size_t(1));
  ASSERT_EQ(set("NEW", "third"), 0);
  EXPECT_EQ(allocations[array_id], uintptr_t(0));
  EXPECT_EQ(env.get("DROP"), nullptr);
  EXPECT_STREQ(env.get("KEEP"), "second");
  EXPECT_EQ(live, 4U);
}

TEST_F(LlvmLibcEnvironmentTest, MutationArgumentsCanReferToDetachedStrings) {
  ASSERT_EQ(set("SOURCE", "value"), 0);
  char *value = env.get("SOURCE");
  unsigned source_id = allocation_id(current_array()[0]);
  replace_array(nullptr);
  ASSERT_EQ(set("COPY", value), 0);
  EXPECT_EQ(allocations[source_id], uintptr_t(0));
  EXPECT_STREQ(env.get("COPY"), "value");

  char *entry = current_array()[0];
  unsigned copy_id = allocation_id(entry);
  replace_array(nullptr);
  ASSERT_EQ(put(entry), 0);
  EXPECT_NE(allocations[copy_id], uintptr_t(0));
  EXPECT_EQ(current_array()[0], entry);
  EXPECT_STREQ(env.get("COPY"), "value");
  ASSERT_EQ(set("NAME", "NAME"), 0);
  ASSERT_EQ(unset(env.get("NAME")), 0);
  EXPECT_EQ(env.get("NAME"), nullptr);
}

TEST_F(LlvmLibcEnvironmentTest, FreedBorrowedArrayIsNeverRevisited) {
  char entry[] = "A=one", other[] = "B=two";
  auto **external = static_cast<char **>(__real_malloc(2 * sizeof(char *)));
  ASSERT_NE(external, nullptr);
  external[0] = entry;
  external[1] = nullptr;
  replace_array(external);
  EXPECT_EQ(env.get("A"), entry + 2);
  char *replacement[] = {other, nullptr};
  replace_array(replacement);
  __real_free(external);
  EXPECT_EQ(env.get("B"), other + 2);
  ASSERT_EQ(set("NEW", "value"), 0);
  EXPECT_EQ(env.get("A"), nullptr);
  EXPECT_EQ(env.get("B"), other + 2);
}

TEST_F(LlvmLibcEnvironmentTest, SetFailureRollsBackEveryAllocation) {
  for (unsigned failure = 1; failure <= 3; ++failure) {
    ASSERT_EQ(set("KEEP", "old"), 0);
    char *kept = current_array()[0];
    char *replacement[] = {kept, nullptr};
    replace_array(replacement);
    auto *old_app = LIBC_NAMESPACE::app.env_ptr;
    unsigned old_live = live;
    fail_on(failure);
    EXPECT_EQ(set("KEEP", env.get("KEEP")), -1);
    EXPECT_EQ(attempts, failure);
    EXPECT_EQ(live, old_live);
    EXPECT_EQ(current_array(), replacement);
    EXPECT_EQ(LIBC_NAMESPACE::app.env_ptr, old_app);
    EXPECT_EQ(replacement[0], kept);
    EXPECT_STREQ(env.get("KEEP"), "old");
    fail_on(0);
    ASSERT_EQ(set("KEEP", env.get("KEEP")), 0);
    EXPECT_STREQ(env.get("KEEP"), "old");
    clear();
    EXPECT_EQ(live, 0U);
  }
}

TEST_F(LlvmLibcEnvironmentTest, PutAndUnsetFailureRollBackBothArrays) {
  for (unsigned remove = 0; remove != 2; ++remove) {
    for (unsigned failure = 1; failure <= 2; ++failure) {
      ASSERT_EQ(set("OLD", "owned"), 0);
      char entry[] = "A=one", duplicate[] = "A=two", added[] = "B=three";
      char *replacement[] = {entry, duplicate, nullptr};
      replace_array(replacement);
      auto *old_app = LIBC_NAMESPACE::app.env_ptr;
      unsigned old_live = live;
      fail_on(failure);
      EXPECT_EQ(remove ? unset("A") : put(added), -1);
      EXPECT_EQ(attempts, failure);
      EXPECT_EQ(live, old_live);
      EXPECT_EQ(current_array(), replacement);
      EXPECT_EQ(LIBC_NAMESPACE::app.env_ptr, old_app);
      EXPECT_EQ(replacement[0], entry);
      EXPECT_EQ(replacement[1], duplicate);
      EXPECT_EQ(replacement[2], nullptr);
      fail_on(0);
      ASSERT_EQ(remove ? unset("A") : put(added), 0);
      EXPECT_EQ(env.get("OLD"), nullptr);
      if (remove)
        EXPECT_EQ(env.get("A"), nullptr);
      else
        EXPECT_EQ(env.get("B"), added + 2);
      clear();
      EXPECT_EQ(live, 0U);
    }
  }
}

TEST_F(LlvmLibcEnvironmentTest, GrowthFailurePreservesOwnedAndBorrowedEntries) {
  ASSERT_EQ(set("OWNED", "value"), 0);
  char entries[31][5];
  for (unsigned i = 0; i < 31; ++i) {
    entries[i][0] = 'X';
    entries[i][1] = static_cast<char>('A' + i);
    entries[i][2] = '=';
    entries[i][3] = 'v';
    entries[i][4] = '\0';
    ASSERT_EQ(put(entries[i]), 0);
  }
  ASSERT_EQ(env.size(), size_t(32));
  for (unsigned failure = 1; failure <= 3; ++failure) {
    char **old_array = current_array();
    unsigned old_live = live;
    fail_on(failure);
    EXPECT_EQ(set("NEW", "value"), -1);
    EXPECT_EQ(attempts, failure);
    EXPECT_EQ(current_array(), old_array);
    EXPECT_EQ(live, old_live);
    EXPECT_EQ(env.size(), size_t(32));
    EXPECT_STREQ(env.get("OWNED"), "value");
    for (unsigned i = 0; i < 31; ++i)
      EXPECT_EQ(current_array()[i + 1], entries[i]);
    EXPECT_EQ(current_array()[32], nullptr);
  }
  fail_on(0);
  ASSERT_EQ(set("NEW", "value"), 0);
  EXPECT_EQ(env.size(), size_t(33));
  EXPECT_STREQ(env.get("NEW"), "value");
}

TEST_F(LlvmLibcEnvironmentTest, ReadsNoOpsAndClearDoNotAllocate) {
  ASSERT_EQ(set("OWNED", "value"), 0);
  char entry[] = "A=one";
  char *replacement[] = {entry, nullptr};
  replace_array(replacement);
  fail_on(1);
  char *value;
  size_t count;
  char **begin, **end;
  {
    TrackAllocations track;
    value = env.get("A");
    count = env.size();
    begin = env.begin();
    end = env.end();
  }
  EXPECT_EQ(value, entry + 2);
  EXPECT_EQ(count, size_t(1));
  EXPECT_EQ(begin, replacement);
  EXPECT_EQ(end, replacement + 1);
  EXPECT_EQ(set("A", "ignored", false), 0);
  EXPECT_EQ(unset("MISSING"), 0);
  EXPECT_EQ(current_array(), replacement);
  clear();
  EXPECT_EQ(attempts, 0U);
  EXPECT_EQ(live, 0U);
  EXPECT_EQ(current_array(), nullptr);
  EXPECT_STREQ(entry, "A=one");
}

#ifdef LIBC_TEST_ENVIRON_MODIFIERS
TEST_F(LlvmLibcEnvironmentTest, PublicModifierAllocationErrors) {
  for (unsigned operation = 0; operation < 3; ++operation) {
    unsigned count = operation == 0 ? 3 : 2;
    for (unsigned failure = 1; failure <= count; ++failure) {
      char name[] = "A", value[] = "new", entry[] = "A=old";
      char added[] = "B=borrowed";
      char *replacement[] = {entry, nullptr};
      replace_array(replacement);
      fail_on(failure);
      libc_errno = EDOM;
      int result;
      {
        TrackAllocations track;
        result = operation == 0 ? LIBC_NAMESPACE::setenv(name, value, 1)
                 : operation == 1 ? LIBC_NAMESPACE::putenv(added)
                                  : LIBC_NAMESPACE::unsetenv(name);
      }
      EXPECT_NE(result, 0);
      if (operation != 1)
        EXPECT_EQ(result, -1);
      EXPECT_EQ(int(libc_errno), ENOMEM);
      EXPECT_EQ(attempts, failure);
      EXPECT_EQ(live, 0U);
      EXPECT_EQ(current_array(), replacement);
      EXPECT_EQ(replacement[0], entry);
      EXPECT_EQ(replacement[1], nullptr);
      EXPECT_STREQ(env.get("A"), "old");
      EXPECT_EQ(env.get("B"), nullptr);
      EXPECT_STREQ(name, "A");
      EXPECT_STREQ(value, "new");
      EXPECT_STREQ(added, "B=borrowed");
      fail_on(0);
      clear();
    }
  }
}

TEST_F(LlvmLibcEnvironmentTest, PublicModifierInvalidNamesPreserveState) {
  char entry[] = "KEEP=value";
  char *replacement[] = {entry, nullptr};
  replace_array(replacement);
  const char *names[] = {"", "BAD=NAME"};
  for (const char *name : names) {
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::setenv(name, "ignored", 1), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::unsetenv(name), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(current_array(), replacement);
    EXPECT_EQ(replacement[0], entry);
    EXPECT_STREQ(env.get("KEEP"), "value");
  }
}
#endif
