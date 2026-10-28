//===-- Tests for private BSD hash page operations ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/hash_page.h"
#include "test/UnitTest/Test.h"

namespace db = LIBC_NAMESPACE::dbm;
using Result = db::HashPageResult;

static bool same(const unsigned char *a, const unsigned char *b, size_t size) {
  for (size_t i = 0; i < size; ++i)
    if (a[i] != b[i])
      return false;
  return true;
}

TEST(LlvmLibcHashPageTest, EmptyBinaryAndUnaligned) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    unsigned char storage[db::HASH_PAGE_SIZE + 2] = {};
    storage[0] = storage[sizeof(storage) - 1] = 0xa5;
    unsigned char *page = storage + 1;
    ASSERT_TRUE(db::initialize_hash_page(page, db::HASH_PAGE_SIZE, be));
    db::HashRecord out{};
    EXPECT_TRUE(db::read_hash_record(page, db::HASH_PAGE_SIZE, be, 0, out) ==
                Result::NotFound);
    const unsigned char key[] = {0, 255, 128, 3};
    const unsigned char data[] = {255, 0, 4};
    const db::HashRecord records[] = {{key, sizeof(key), data, sizeof(data)},
                                      {nullptr, 0, data, sizeof(data)},
                                      {key, sizeof(key), nullptr, 0},
                                      {nullptr, 0, nullptr, 0}};
    for (auto record : records)
      ASSERT_TRUE(db::append_hash_record(page, db::HASH_PAGE_SIZE, be,
                                         record) == Result::Success);
    for (size_t i = 0; i < 4; ++i) {
      ASSERT_TRUE(db::read_hash_record(page, db::HASH_PAGE_SIZE, be, i, out) ==
                  Result::Success);
      EXPECT_EQ(out.key_size, records[i].key_size);
      EXPECT_EQ(out.content_size, records[i].content_size);
      EXPECT_TRUE(same(out.key, records[i].key, out.key_size));
      EXPECT_TRUE(same(out.content, records[i].content, out.content_size));
    }
    EXPECT_EQ(storage[0], static_cast<unsigned char>(0xa5));
    EXPECT_EQ(storage[sizeof(storage) - 1], static_cast<unsigned char>(0xa5));
  }
}

TEST(LlvmLibcHashPageTest, RecordCapacityAndFailureOwnership) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    unsigned char page[db::HASH_PAGE_SIZE], before[db::HASH_PAGE_SIZE];
    unsigned char data[db::HASH_RECORD_LIMIT + 1];
    for (size_t i = 0; i < sizeof(data); ++i)
      data[i] = static_cast<unsigned char>(i);
    ASSERT_TRUE(db::initialize_hash_page(page, sizeof(page), be));
    ASSERT_TRUE(db::append_hash_record(page, sizeof(page), be,
                                       {data, 1023, data + 1023, 3059}) ==
                Result::Success);
    for (size_t i = 0; i < sizeof(page); ++i)
      before[i] = page[i];
    EXPECT_TRUE(db::append_hash_record(page, sizeof(page), be,
                                       {nullptr, 0, nullptr, 0}) ==
                Result::Full);
    EXPECT_TRUE(same(page, before, sizeof(page)));
    EXPECT_TRUE(db::append_hash_record(page, sizeof(page), be,
                                       {data, sizeof(data), nullptr, 0}) ==
                Result::RecordTooLarge);
    EXPECT_TRUE(db::append_hash_record(page, sizeof(page), be,
                                       {nullptr, size_t(-1), nullptr, 0}) ==
                Result::RecordTooLarge);
    EXPECT_TRUE(same(page, before, sizeof(page)));
    // Even a maximum-size record leaves room to extend the bucket chain.
    ASSERT_TRUE(db::set_hash_overflow(page, sizeof(page), be, 2050) ==
                Result::Success);
    db::HashRecord out{};
    ASSERT_TRUE(db::read_hash_record(page, sizeof(page), be, 0, out) ==
                Result::Success);
    EXPECT_TRUE(same(out.key, data, 1023));
    EXPECT_TRUE(same(out.content, data + 1023, 3059));
    EXPECT_TRUE(db::read_hash_record(page, sizeof(page), be, 1, out) ==
                Result::NotFound);
  }
}

TEST(LlvmLibcHashPageTest, DeleteCompactsAndPreservesOverflow) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    for (size_t removed = 0; removed < 3; ++removed) {
      unsigned char page[db::HASH_PAGE_SIZE];
      ASSERT_TRUE(db::initialize_hash_page(page, sizeof(page), be));
      ASSERT_TRUE(db::set_hash_overflow(page, sizeof(page), be, 2050) ==
                  Result::Success);
      const unsigned char data[][4] = {
          {1, 0, 2, 3}, {4, 5, 6, 7}, {8, 9, 0, 1}};
      for (auto &bytes : data)
        ASSERT_TRUE(db::append_hash_record(page, sizeof(page), be,
                                           {bytes, 4, bytes, 4}) ==
                    Result::Success);
      ASSERT_TRUE(db::remove_hash_record(page, sizeof(page), be, removed) ==
                  Result::Success);
      size_t output = 0;
      for (size_t i = 0; i < 3; ++i) {
        if (i == removed)
          continue;
        db::HashRecord out{};
        ASSERT_TRUE(db::read_hash_record(page, sizeof(page), be, output++,
                                         out) == Result::Success);
        EXPECT_EQ(out.key_size, size_t(4));
        EXPECT_EQ(out.content_size, size_t(4));
        EXPECT_TRUE(same(out.key, data[i], 4));
        EXPECT_TRUE(same(out.content, data[i], 4));
      }
      // Three removals leave a valid page containing only its overflow link.
      ASSERT_TRUE(db::remove_hash_record(page, sizeof(page), be, 0) ==
                  Result::Success);
      ASSERT_TRUE(db::remove_hash_record(page, sizeof(page), be, 0) ==
                  Result::Success);
      EXPECT_TRUE(db::validate_hash_page(page, sizeof(page), be) ==
                  db::HashPageStatus::Valid);
      EXPECT_EQ(page[be ? 3 : 2], static_cast<unsigned char>(2));
      EXPECT_EQ(page[be ? 2 : 3], static_cast<unsigned char>(8));
      EXPECT_TRUE(db::remove_hash_record(page, sizeof(page), be, 0) ==
                  Result::NotFound);
    }
  }
}

TEST(LlvmLibcHashPageTest, DenseEmptyRecordsAndReuse) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    unsigned char page[db::HASH_PAGE_SIZE];
    ASSERT_TRUE(db::initialize_hash_page(page, sizeof(page), be));
    // Empty records still consume two offsets and reserve an overflow pair.
    constexpr size_t capacity = (db::HASH_PAGE_SIZE - 6 - 4) / 4;
    for (size_t i = 0; i < capacity; ++i)
      ASSERT_TRUE(db::append_hash_record(page, sizeof(page), be,
                                         {nullptr, 0, nullptr, 0}) ==
                  Result::Success);
    EXPECT_TRUE(db::append_hash_record(page, sizeof(page), be,
                                       {nullptr, 0, nullptr, 0}) ==
                Result::Full);
    for (size_t i = capacity; i; --i)
      ASSERT_TRUE(db::remove_hash_record(page, sizeof(page), be, i / 2) ==
                  Result::Success);
    const unsigned char data[] = {1, 2};
    EXPECT_TRUE(db::append_hash_record(page, sizeof(page), be,
                                       {data, 2, data, 2}) == Result::Success);
  }
}

TEST(LlvmLibcHashPageTest, MalformedInputDoesNotWrite) {
  unsigned char page[db::HASH_PAGE_SIZE] = {};
  db::HashRecord out{nullptr, 123, nullptr, 456};
  EXPECT_FALSE(db::initialize_hash_page(nullptr, sizeof(page), true));
  EXPECT_FALSE(db::initialize_hash_page(page, sizeof(page) - 1, true));
  EXPECT_TRUE(db::read_hash_record(page, sizeof(page), true, 0, out) ==
              Result::Malformed);
  EXPECT_EQ(out.key_size, size_t(123));
  EXPECT_EQ(out.content_size, size_t(456));
  EXPECT_TRUE(db::append_hash_record(page, sizeof(page), true,
                                     {nullptr, 0, nullptr, 0}) ==
              Result::Malformed);
  EXPECT_TRUE(db::remove_hash_record(page, sizeof(page), true, 0) ==
              Result::Malformed);
  EXPECT_TRUE(db::set_hash_overflow(page, sizeof(page), true, 2050) ==
              Result::Malformed);
  ASSERT_TRUE(db::initialize_hash_page(page, sizeof(page), true));
  unsigned char before[db::HASH_PAGE_SIZE];
  for (size_t i = 0; i < sizeof(page); ++i)
    before[i] = page[i];
  const uint16_t bad_addresses[] = {0, 2048, uint16_t((31 << 11) | 1)};
  for (auto address : bad_addresses)
    EXPECT_TRUE(db::set_hash_overflow(page, sizeof(page), true, address) ==
                Result::Malformed);
  EXPECT_TRUE(db::append_hash_record(page, sizeof(page), true,
                                     {nullptr, 1, nullptr, 0}) ==
              Result::Malformed);
  EXPECT_TRUE(db::remove_hash_record(page, sizeof(page), true, size_t(-1)) ==
              Result::NotFound);
  EXPECT_TRUE(same(page, before, sizeof(page)));
}
