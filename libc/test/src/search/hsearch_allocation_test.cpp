//===-- Tests for hash search allocation failures -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/HashTable/table.h"
#include "src/__support/libc_errno.h"
#include "src/search/hcreate.h"
#include "src/search/hdestroy.h"
#include "src/search/hsearch.h"
#include "src/search/hsearch/global.h"
#include "test/UnitTest/Test.h"

namespace {
bool track = false, fail = false;
unsigned attempts = 0, releases = 0, invalid_releases = 0;
void *owned[8]{};
using Table = LIBC_NAMESPACE::internal::HashTable;
} // namespace

extern "C" void *__real_aligned_alloc(size_t, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_aligned_alloc(size_t alignment, size_t size) {
  if (track) {
    ++attempts;
    if (fail)
      return nullptr;
  }
  void *result = __real_aligned_alloc(alignment, size);
  if (track && result) {
    for (auto &entry : owned) {
      if (!entry) {
        entry = result;
        return result;
      }
    }
    __builtin_trap();
  }
  return result;
}
extern "C" void __wrap_free(void *ptr) {
  if (track && ptr) {
    for (auto &entry : owned) {
      if (entry == ptr) {
        entry = nullptr;
        ++releases;
        __real_free(ptr);
        return;
      }
    }
    ++invalid_releases;
    return;
  }
  __real_free(ptr);
}

TEST(LlvmLibcHSearchAllocationTest, RejectAllocationBeforeComputingAddress) {
  attempts = releases = 0;
  fail = track = true;
  Table *table = Table::allocate(16, 1);
  track = false;
  EXPECT_EQ(table, static_cast<Table *>(nullptr));
  EXPECT_EQ(attempts, 1U);
  EXPECT_EQ(releases, 0U);
}

TEST(LlvmLibcHSearchAllocationTest, FailedCreationLeavesNoTable) {
  LIBC_NAMESPACE::hdestroy();
  attempts = releases = invalid_releases = 0;
  fail = track = true;
  libc_errno = 0;
  int created = LIBC_NAMESPACE::hcreate(16);
  int error = libc_errno;
  LIBC_NAMESPACE::hdestroy();
  track = false;
  EXPECT_EQ(created, 0);
  EXPECT_EQ(error, ENOMEM);
  EXPECT_EQ(LIBC_NAMESPACE::internal::global_hash_table, nullptr);
  EXPECT_EQ(attempts, 1U);
  EXPECT_EQ(releases, 0U);
  EXPECT_EQ(invalid_releases, 0U);

  char key[] = "key";
  track = true;
  ENTRY *result = LIBC_NAMESPACE::hsearch({key, key}, ENTER);
  error = libc_errno;
  track = false;
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(error, ENOMEM);
  EXPECT_EQ(LIBC_NAMESPACE::internal::global_hash_table, nullptr);
  EXPECT_EQ(attempts, 2U);

  fail = false;
  track = true;
  created = LIBC_NAMESPACE::hcreate(16);
  LIBC_NAMESPACE::hdestroy();
  track = false;
  EXPECT_EQ(created, 1);
  EXPECT_EQ(attempts, 3U);
  EXPECT_EQ(releases, 1U);
  EXPECT_EQ(invalid_releases, 0U);
}

TEST(LlvmLibcHSearchAllocationTest, FailedGrowthPreservesEntriesAndOwnership) {
  LIBC_NAMESPACE::hdestroy();
  attempts = releases = invalid_releases = 0;
  fail = false;
  track = true;
  int created = LIBC_NAMESPACE::hcreate(0);
  track = false;
  ASSERT_EQ(created, 1);
  Table *original = LIBC_NAMESPACE::internal::global_hash_table;
  size_t capacity = original->available_slots;
  char keys[64][3]{};
  int data[64]{};
  ASSERT_LT(capacity, size_t{64});
  for (size_t i = 0; i <= capacity; ++i) {
    keys[i][0] = static_cast<char>('A' + i / 26);
    keys[i][1] = static_cast<char>('a' + i % 26);
    data[i] = static_cast<int>(i);
  }
  for (size_t i = 0; i < capacity; ++i) {
    track = true;
    ENTRY *entry = LIBC_NAMESPACE::hsearch({keys[i], &data[i]}, ENTER);
    track = false;
    ASSERT_NE(entry, nullptr);
  }
  ASSERT_EQ(attempts, 1U);
  ASSERT_EQ(original->available_slots, size_t{0});

  fail = track = true;
  ENTRY *rejected =
      LIBC_NAMESPACE::hsearch({keys[capacity], &data[capacity]}, ENTER);
  int error = libc_errno;
  ENTRY *duplicate = LIBC_NAMESPACE::hsearch({keys[0], &data[capacity]}, ENTER);
  track = false;
  EXPECT_EQ(rejected, nullptr);
  EXPECT_EQ(error, ENOMEM);
  EXPECT_EQ(LIBC_NAMESPACE::internal::global_hash_table, original);
  ASSERT_NE(duplicate, nullptr);
  EXPECT_EQ(duplicate->key, keys[0]);
  EXPECT_EQ(duplicate->data, static_cast<void *>(&data[0]));
  EXPECT_EQ(attempts, 2U);
  EXPECT_EQ(releases, 0U);
  for (size_t i = 0; i < capacity; ++i) {
    ENTRY *entry = LIBC_NAMESPACE::hsearch({keys[i], nullptr}, FIND);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->key, keys[i]);
    EXPECT_EQ(entry->data, static_cast<void *>(&data[i]));
  }
  EXPECT_EQ(LIBC_NAMESPACE::hsearch({keys[capacity], nullptr}, FIND), nullptr);

  fail = false;
  track = true;
  ENTRY *added =
      LIBC_NAMESPACE::hsearch({keys[capacity], &data[capacity]}, ENTER);
  track = false;
  ASSERT_NE(added, nullptr);
  EXPECT_EQ(attempts, 3U);
  EXPECT_EQ(releases, 1U);
  for (size_t i = 0; i <= capacity; ++i) {
    ENTRY *entry = LIBC_NAMESPACE::hsearch({keys[i], nullptr}, FIND);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->key, keys[i]);
    EXPECT_EQ(entry->data, static_cast<void *>(&data[i]));
    EXPECT_EQ(data[i], static_cast<int>(i));
  }
  track = true;
  LIBC_NAMESPACE::hdestroy();
  track = false;
  EXPECT_EQ(releases, 2U);
  EXPECT_EQ(invalid_releases, 0U);
  EXPECT_EQ(LIBC_NAMESPACE::internal::global_hash_table, nullptr);
  for (auto *entry : owned)
    EXPECT_EQ(entry, nullptr);
}
