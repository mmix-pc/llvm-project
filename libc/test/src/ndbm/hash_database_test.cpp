//===-- Tests for complete BSD hash validation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/ndbm/hash_database.h"
#include "src/ndbm/hash_function.h"
#include "test/UnitTest/Test.h"

namespace db = LIBC_NAMESPACE::dbm;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
bool track;
unsigned allocations, releases, fail_at;

struct Storage {
  unsigned char pages[5][db::HASH_PAGE_SIZE]{};
  db::HashHeader header{};
  bool fail_after_allocation = false;
  size_t reads = 0;

  explicit Storage(bool be) {
    header.byte_order = be ? 4321 : 1234;
    header.directory_size = 1;
    header.overflow_point = 1;
    header.max_bucket = header.low_mask = 1;
    header.high_mask = 3;
    header.fill_factor = 4;
    header.spares[1] = 2;
    header.bitmaps[0] = 2049;
    for (unsigned p = 1; p < 5; ++p)
      if (p != 3)
        EXPECT_TRUE(db::initialize_hash_page(pages[p], db::HASH_PAGE_SIZE, be));
    pages[3][be ? 3 : 0] = 3;
    EXPECT_TRUE(db::set_hash_overflow(pages[1], db::HASH_PAGE_SIZE, be, 2050) ==
                db::HashPageResult::Success);
  }
  void add(const unsigned char *key, size_t size, bool overflow = false,
           unsigned char value = 1) {
    unsigned page = db::hash_key_bucket(header, key, size) + 1;
    if (overflow) {
      EXPECT_EQ(page, 1U);
      page = 4;
    }
    db::HashRecord record{key, size, &value, 1};
    ASSERT_TRUE(db::append_hash_record(pages[page], db::HASH_PAGE_SIZE,
                                       header.byte_order == 4321,
                                       record) == db::HashPageResult::Success);
    ++header.keys;
  }
  static ErrorOr<size_t> read(void *context, unsigned char *out, size_t size,
                              uint64_t offset) {
    auto &s = *static_cast<Storage *>(context);
    ++s.reads;
    if (s.fail_after_allocation && allocations)
      return Error(EINTR);
    if (offset >= sizeof(s.pages))
      return size_t{0};
    if (size > sizeof(s.pages) - offset)
      size = sizeof(s.pages) - offset;
    for (size_t i = 0; i < size; ++i)
      out[i] = s.pages[(offset + i) / db::HASH_PAGE_SIZE]
                      [(offset + i) % db::HASH_PAGE_SIZE];
    return size;
  }
  ErrorOr<size_t> validate(unsigned fail = 0) {
    allocations = releases = 0;
    fail_at = fail;
    track = true;
    auto result = db::validate_hash_database({this, read, nullptr}, header,
                                             sizeof(pages));
    track = false;
    return result;
  }
};
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (track && ++allocations == fail_at)
    return nullptr;
  return __real_malloc(size);
}
extern "C" void __wrap_free(void *p) {
  if (track && p)
    ++releases;
  __real_free(p);
}

TEST(LlvmLibcHashDatabaseTest, EmptyAndSingleRecordNeedNoAllocation) {
  Storage s(true);
  auto result = s.validate(1);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, size_t{0});
  EXPECT_EQ(allocations, 0U);
  s.add(nullptr, 0);
  result = s.validate(1);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, size_t{1});
  EXPECT_EQ(allocations, 0U);
}

TEST(LlvmLibcHashDatabaseTest, BinaryPrefixesAndReverseOrderBothByteOrders) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage s(be);
    s.add(nullptr, 0);
    for (unsigned i = 256; i > 0; --i) {
      const unsigned char key[] = {0, static_cast<unsigned char>(i - 1), 0};
      s.add(key, sizeof(key));
    }
    const unsigned char prefix[] = {0, 0};
    s.add(prefix, 1);
    s.add(prefix, 2);
    auto result = s.validate();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, size_t{259});
    EXPECT_EQ(allocations, 4U);
    EXPECT_EQ(releases, allocations);
  }
}

TEST(LlvmLibcHashDatabaseTest, DuplicateAcrossPagesAndEmptyKeys) {
  const bool orders[] = {false, true};
  const size_t sizes[] = {0, 3};
  for (bool be : orders)
    for (size_t size : sizes) {
      Storage s(be);
      const unsigned char key[] = {0, 255, 1};
      ASSERT_EQ(db::hash_key_bucket(s.header, key, size), 0U);
      s.add(key, size, false, 1);
      s.add(key, size, true, 2);
      auto result = s.validate();
      ASSERT_FALSE(result.has_value());
      EXPECT_EQ(result.error(), EINVAL);
      EXPECT_EQ(releases, allocations);
    }
}

TEST(LlvmLibcHashDatabaseTest, AllocationAndSecondPassReadFailure) {
  Storage s(true);
  const unsigned char a = 'b', b = 'd';
  s.add(&a, 1);
  s.add(&b, 1, true);
  for (unsigned fail = 1; fail <= 2; ++fail) {
    auto result = s.validate(fail);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ENOMEM);
    EXPECT_EQ(allocations, fail);
    EXPECT_EQ(releases, fail - 1);
  }
  s.fail_after_allocation = true;
  auto result = s.validate();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINTR);
  EXPECT_EQ(releases, allocations);
  s.fail_after_allocation = false;
  result = s.validate();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, size_t{2});
  EXPECT_EQ(releases, allocations);
}

TEST(LlvmLibcHashDatabaseTest, StructuralFailurePrecedesAllocation) {
  Storage s(true);
  s.header.keys = 1;
  auto result = s.validate(1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(allocations, 0U);
}
