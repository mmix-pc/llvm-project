//===-- Tests for checked BSD hash bucket growth --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/ndbm/hash_function.h"
#include "src/ndbm/hash_growth.h"
#include "test/UnitTest/Test.h"

namespace db = LIBC_NAMESPACE::dbm;

namespace {
db::HashHeader initial() {
  db::HashHeader h{};
  h.byte_order = 4321;
  h.directory_size = 1;
  h.overflow_point = 1;
  h.max_bucket = h.low_mask = 1;
  h.high_mask = 3;
  h.fill_factor = 4;
  h.keys = 7;
  h.spares[1] = 3;
  h.bitmaps[0] = 2049;
  return h;
}
} // namespace

TEST(LlvmLibcHashGrowthTest, SplitCycleAndDirectoryGrowth) {
  auto h = initial();
  for (uint32_t n = 2; n <= 1024; ++n) {
    auto result = db::plan_hash_split(h);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->new_bucket, n);
    EXPECT_EQ(result->old_bucket, n & h.low_mask);
    EXPECT_EQ(result->header.keys, h.keys);
    EXPECT_EQ(result->header.fill_factor, h.fill_factor);
    EXPECT_EQ(result->header.byte_order, h.byte_order);
    EXPECT_EQ(result->header.bitmaps[0], h.bitmaps[0]);
    EXPECT_LE(n / 256 + 1, result->header.directory_size);
    for (uint32_t bucket = 0; bucket <= h.max_bucket; ++bucket) {
      uint32_t before, after;
      ASSERT_TRUE(db::hash_bucket_page(h, bucket, before));
      ASSERT_TRUE(db::hash_bucket_page(result->header, bucket, after));
      EXPECT_EQ(before, after);
      EXPECT_NE(after, result->new_page);
    }
    for (uint16_t address = 2049; address <= 2051; ++address) {
      uint32_t before, after;
      ASSERT_TRUE(db::hash_overflow_page(h, address, before));
      ASSERT_TRUE(db::hash_overflow_page(result->header, address, after));
      EXPECT_EQ(before, after);
      EXPECT_NE(after, result->new_page);
    }
    h = result->header;
  }
}

TEST(LlvmLibcHashGrowthTest, OnlySelectedBucketChangesMembership) {
  auto h = initial();
  for (unsigned n = 0; n < 32; ++n) {
    auto result = db::plan_hash_split(h);
    ASSERT_TRUE(result.has_value());
    for (unsigned i = 0; i < 4096; ++i) {
      unsigned char key[] = {static_cast<unsigned char>(i >> 8),
                             static_cast<unsigned char>(i)};
      uint32_t before = db::hash_key_bucket(h, key, sizeof(key));
      uint32_t after = db::hash_key_bucket(result->header, key, sizeof(key));
      EXPECT_TRUE(before == after || (before == result->old_bucket &&
                                      after == result->new_bucket));
    }
    h = result->header;
  }
}

TEST(LlvmLibcHashGrowthTest, InvalidAndExhaustedMetadataPreserved) {
  auto h = initial();
  h.overflow_point = 32;
  auto result = db::plan_hash_split(h);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(h.overflow_point, uint32_t{32});

  h = initial();
  h.directory_size = 1U << 22;
  h.overflow_point = 30;
  h.max_bucket = h.low_mask = (1U << 30) - 1;
  h.high_mask = 0x7fffffff;
  for (unsigned i = 2; i <= 30; ++i)
    h.spares[i] = 3;
  unsigned char encoded[db::HASH_HEADER_SIZE];
  ASSERT_TRUE(db::encode_hash_header(h, db::hash_signature(), encoded,
                                     sizeof(encoded)));
  result = db::plan_hash_split(h);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EFBIG);
  EXPECT_EQ(h.max_bucket, (1U << 30) - 1);
  EXPECT_EQ(h.overflow_point, uint32_t{30});
}
