//===-- Tests for checked hash overflow allocation ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/ndbm/hash_overflow.h"
#include "test/UnitTest/Test.h"

namespace db = LIBC_NAMESPACE::dbm;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
struct Storage {
  db::HashHeader header{};
  unsigned char maps[2][db::HASH_PAGE_SIZE];
  unsigned reads = 0;
  int error = 0;
  bool truncated = false;

  explicit Storage(uint32_t total, bool be = true) {
    header.byte_order = be ? 4321 : 1234;
    header.directory_size = 1;
    header.max_bucket = header.low_mask = 1;
    header.high_mask = 3;
    header.fill_factor = 4;
    header.last_freed = total;
    uint32_t remaining = total;
    for (uint32_t split = 1; remaining; ++split) {
      uint32_t count = remaining < 2047 ? remaining : 2047;
      remaining -= count;
      header.spares[split] = total - remaining;
      header.overflow_point = split;
    }
    header.bitmaps[0] = 2049;
    if (total > db::HASH_PAGE_SIZE * 8)
      header.bitmaps[1] = address(db::HASH_PAGE_SIZE * 8);
    for (auto &map : maps)
      for (auto &byte : map)
        byte = 0xff;
  }
  uint16_t address(uint32_t bit) const {
    for (uint32_t split = 0; split <= header.overflow_point; ++split) {
      if (bit < header.spares[split]) {
        uint32_t previous = split ? header.spares[split - 1] : 0;
        return static_cast<uint16_t>((split << 11) | (bit - previous + 1));
      }
    }
    return 0;
  }
  void clear(uint32_t bit) {
    uint32_t local = bit % (db::HASH_PAGE_SIZE * 8);
    size_t byte =
        (local / 32) * 4 +
        (header.byte_order == 4321 ? 3 - (local % 32) / 8 : (local % 32) / 8);
    maps[bit / (db::HASH_PAGE_SIZE * 8)][byte] &=
        static_cast<unsigned char>(~(1U << (local % 8)));
  }
  static ErrorOr<size_t> read(void *context, unsigned char *out, size_t size,
                              uint64_t offset) {
    auto &s = *static_cast<Storage *>(context);
    ++s.reads;
    if (s.error)
      return Error(s.error);
    if (s.truncated)
      return size_t{0};
    for (unsigned map = 0; map < 2; ++map) {
      uint32_t physical;
      if (!db::hash_overflow_page(s.header, s.header.bitmaps[map], physical))
        continue;
      if (offset != uint64_t(physical) * db::HASH_PAGE_SIZE)
        continue;
      if (size != db::HASH_PAGE_SIZE)
        return Error(EINVAL);
      for (size_t i = 0; i < size; ++i)
        out[i] = s.maps[map][i];
      return size;
    }
    return Error(EIO);
  }
  db::HashIO io() { return {this, read, nullptr}; }
  void apply(const db::HashOverflow &plan) {
    header = plan.header;
    for (unsigned map = 0; map < 2; ++map) {
      uint32_t physical;
      if (db::hash_overflow_page(header, header.bitmaps[map], physical) &&
          physical == plan.bitmap_physical) {
        for (size_t i = 0; i < db::HASH_PAGE_SIZE; ++i)
          maps[map][i] = plan.bitmap[i];
        return;
      }
    }
    EXPECT_TRUE(false);
  }
};
} // namespace

TEST(LlvmLibcHashOverflowTest, ReuseAndReleaseBothOrders) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage s(35, be);
    s.clear(1);
    s.clear(31);
    s.clear(32);
    const uint32_t bits[] = {1, 31, 32};
    for (uint32_t bit : bits) {
      auto result = db::plan_hash_overflow(s.io(), s.header);
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(result->address, s.address(bit));
      EXPECT_EQ(result->header.spares[1], 35U);
      EXPECT_EQ(result->header.last_freed, bit);
      auto repeated = db::plan_hash_overflow(s.io(), s.header);
      ASSERT_TRUE(repeated.has_value());
      EXPECT_EQ(repeated->address, result->address);
      s.apply(*result);
    }
    auto released = db::plan_hash_release(s.io(), s.header, s.address(1));
    ASSERT_TRUE(released.has_value());
    EXPECT_EQ(released->header.last_freed, 1U);
    s.apply(*released);
    auto twice = db::plan_hash_release(s.io(), s.header, s.address(1));
    ASSERT_FALSE(twice.has_value());
    EXPECT_EQ(twice.error(), EINVAL);
    auto reused = db::plan_hash_overflow(s.io(), s.header);
    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(reused->address, s.address(1));
  }
}

TEST(LlvmLibcHashOverflowTest, OffsetLimitPreservesExistingAddresses) {
  Storage s(2046);
  auto last = db::plan_hash_overflow(s.io(), s.header);
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->address, uint16_t{4095});
  EXPECT_EQ(last->header.spares[1], 2047U);
  EXPECT_EQ(s.header.spares[1], 2046U);
  s.apply(*last);
  auto next = db::plan_hash_overflow(s.io(), s.header);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->header.overflow_point, 2U);
  EXPECT_EQ(next->header.spares[1], 2047U);
  EXPECT_EQ(next->header.spares[2], 2048U);
  EXPECT_EQ(next->address, uint16_t{4097});
  EXPECT_EQ(next->physical, 2052U);
  for (uint16_t address = 2049; address <= 4095; ++address) {
    uint32_t before, after;
    ASSERT_TRUE(db::hash_overflow_page(s.header, address, before));
    ASSERT_TRUE(db::hash_overflow_page(next->header, address, after));
    EXPECT_EQ(before, after);
    EXPECT_NE(after, next->physical);
  }
}

TEST(LlvmLibcHashOverflowTest, NewBitmapOwnsItsPageBeforeDataPage) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage s(32768, be);
    auto result = db::plan_hash_overflow(s.io(), s.header);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header.spares[17], 32770U);
    EXPECT_EQ(result->header.bitmaps[1], uint16_t{34833});
    EXPECT_EQ(result->address, uint16_t{34834});
    EXPECT_NE(result->physical, result->bitmap_physical);
    for (auto byte : result->bitmap)
      EXPECT_EQ(byte, static_cast<unsigned char>(0xff));
    EXPECT_EQ(s.header.spares[17], 32768U);
    s.apply(*result);
    auto free = db::plan_hash_release(s.io(), s.header, result->address);
    ASSERT_TRUE(free.has_value());
    s.apply(*free);
    auto reuse = db::plan_hash_overflow(s.io(), s.header);
    ASSERT_TRUE(reuse.has_value());
    EXPECT_EQ(reuse->address, result->address);
    EXPECT_EQ(reuse->physical, result->physical);
    EXPECT_EQ(reuse->header.spares[17], 32770U);
  }
}

TEST(LlvmLibcHashOverflowTest, RejectBitmapAndMalformedRelease) {
  Storage s(35);
  const uint16_t addresses[] = {0, 2049, 2084, 0xffff};
  for (uint16_t address : addresses) {
    auto result = db::plan_hash_release(s.io(), s.header, address);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EINVAL);
  }
  EXPECT_EQ(s.reads, 0U);
  s.clear(0);
  auto result = db::plan_hash_overflow(s.io(), s.header);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
}

TEST(LlvmLibcHashOverflowTest, ErrorsDoNotWriteOrChangeInputs) {
  Storage s(35);
  s.error = EINTR;
  auto result = db::plan_hash_overflow(s.io(), s.header);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINTR);
  EXPECT_EQ(s.reads, 1U);
  s.error = 0;
  s.truncated = true;
  result = db::plan_hash_release(s.io(), s.header, 2050);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EIO);
  s.truncated = false;
  s.header.overflow_point = 31;
  result = db::plan_hash_overflow(s.io(), s.header);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(s.reads, 2U);
  EXPECT_EQ(s.header.spares[1], 35U);
  for (auto byte : s.maps[0])
    EXPECT_EQ(byte, static_cast<unsigned char>(0xff));
}

TEST(LlvmLibcHashOverflowTest, ExhaustedAddressSpacePreserved) {
  Storage s(30 * 2047);
  auto result = db::plan_hash_overflow(s.io(), s.header);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EFBIG);
  EXPECT_EQ(s.header.overflow_point, 30U);
  EXPECT_EQ(s.header.spares[30], 30U * 2047);
  EXPECT_EQ(s.reads, 2U);
  // Capacity exhaustion does not prevent reuse of an existing free page.
  s.clear(61409);
  auto reused = db::plan_hash_overflow(s.io(), s.header);
  ASSERT_TRUE(reused.has_value());
  EXPECT_EQ(reused->address, uint16_t{63487});
}
