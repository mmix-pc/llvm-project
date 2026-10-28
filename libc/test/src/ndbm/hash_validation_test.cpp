//===-- Tests for private DBM format bounds -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/hash_validation.h"
#include "test/UnitTest/Test.h"

namespace db = LIBC_NAMESPACE::dbm;
using Status = db::HashPageStatus;

static void word(unsigned char *out, size_t index, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[4 * index + i] = static_cast<unsigned char>(value >> (24 - 8 * i));
}
static void half(unsigned char *out, size_t index, uint16_t value, bool be) {
  out[2 * index] = static_cast<unsigned char>(be ? value >> 8 : value);
  out[2 * index + 1] = static_cast<unsigned char>(be ? value : value >> 8);
}
static void header(unsigned char *out) {
  for (size_t i = 0; i < db::HASH_HEADER_SIZE; ++i)
    out[i] = 0;
  word(out, 0, 0x061561);
  word(out, 1, 2);
  word(out, 2, 4321);
  word(out, 3, 4096);
  word(out, 4, 12);
  word(out, 5, 256);
  word(out, 6, 256);
  word(out, 7, 8);
  word(out, 8, 1);
  word(out, 9, 2);
  word(out, 10, 1);
  word(out, 11, 3);
  word(out, 12, 1);
  word(out, 13, 40);
  word(out, 15, 1);
  word(out, 16, 123);
  word(out, 18, 2);
  word(out, 19, 2);
  half(out + 196, 0, 2049, true);
}

TEST(LlvmLibcHashValidationTest, HeaderAndUnalignedBytes) {
  unsigned char storage[db::HASH_HEADER_SIZE + 1];
  unsigned char *bytes = storage + 1;
  header(bytes);
  db::HashHeader decoded{};
  ASSERT_TRUE(
      db::decode_hash_header(bytes, db::HASH_HEADER_SIZE, 123, decoded));
  EXPECT_EQ(decoded.byte_order, uint32_t(4321));
  EXPECT_EQ(decoded.max_bucket, uint32_t(1));
  EXPECT_EQ(decoded.bitmaps[0], uint16_t(2049));
  EXPECT_FALSE(
      db::decode_hash_header(bytes, db::HASH_HEADER_SIZE, 124, decoded));
  word(bytes, 2, 1234);
  EXPECT_TRUE(
      db::decode_hash_header(bytes, db::HASH_HEADER_SIZE, 123, decoded));
  for (size_t size = 0; size < db::HASH_HEADER_SIZE; ++size)
    EXPECT_FALSE(db::decode_hash_header(bytes, size, 123, decoded));
  EXPECT_FALSE(
      db::decode_hash_header(nullptr, db::HASH_HEADER_SIZE, 123, decoded));
}

TEST(LlvmLibcHashValidationTest, HeaderArithmeticAndFailureOwnership) {
  struct Mutation {
    size_t index;
    uint32_t value;
  };
  constexpr Mutation mutations[] = {
      {0, 0},           {1, 1},  {1, 3},  {2, 0},           {3, 0},  {3, 65536},
      {4, 32},          {5, 0},  {5, 3},  {5, 0x80000000},  {6, 0},  {7, 31},
      {8, 31},          {9, 3},  {10, 4}, {11, 7},          {12, 0}, {13, 0},
      {14, 0x80000000}, {15, 0}, {17, 3}, {18, 0xffffffff}, {18, 0}};
  for (auto mutation : mutations) {
    unsigned char bytes[db::HASH_HEADER_SIZE];
    header(bytes);
    word(bytes, mutation.index, mutation.value);
    db::HashHeader decoded{};
    decoded.keys = 987;
    EXPECT_FALSE(db::decode_hash_header(bytes, sizeof(bytes), 123, decoded));
    EXPECT_EQ(decoded.keys, uint32_t(987));
  }
  unsigned char bytes[db::HASH_HEADER_SIZE];
  header(bytes);
  half(bytes + 196, 0, 0, true);
  db::HashHeader decoded{};
  EXPECT_FALSE(db::decode_hash_header(bytes, sizeof(bytes), 123, decoded));
  half(bytes + 196, 0, uint16_t((31 << 11) | 1), true);
  EXPECT_FALSE(db::decode_hash_header(bytes, sizeof(bytes), 123, decoded));
}

TEST(LlvmLibcHashValidationTest, EncodedHeaderBytesAndFailureOwnership) {
  unsigned char reference[db::HASH_HEADER_SIZE];
  unsigned char storage[db::HASH_HEADER_SIZE + 2];
  const uint32_t orders[] = {1234, 4321};
  for (uint32_t order : orders) {
    header(reference);
    word(reference, 2, order);
    word(reference, 14, 0x123456);
    db::HashHeader decoded{};
    ASSERT_TRUE(
        db::decode_hash_header(reference, sizeof(reference), 123, decoded));
    for (auto &byte : storage)
      byte = 0xa5;
    ASSERT_TRUE(db::encode_hash_header(decoded, 123, storage + 1,
                                       db::HASH_HEADER_SIZE));
    EXPECT_EQ(storage[0], static_cast<unsigned char>(0xa5));
    EXPECT_EQ(storage[sizeof(storage) - 1], static_cast<unsigned char>(0xa5));
    for (size_t i = 0; i < sizeof(reference); ++i)
      EXPECT_EQ(storage[1 + i], reference[i]);
    // Persistent scalar fields remain big-endian for either page order.
    EXPECT_EQ(storage[1 + 56], static_cast<unsigned char>(0));
    EXPECT_EQ(storage[1 + 57], static_cast<unsigned char>(0x12));
    EXPECT_EQ(storage[1 + 58], static_cast<unsigned char>(0x34));
    EXPECT_EQ(storage[1 + 59], static_cast<unsigned char>(0x56));
    EXPECT_EQ(storage[1 + 196], static_cast<unsigned char>(8));
    EXPECT_EQ(storage[1 + 197], static_cast<unsigned char>(1));
    for (size_t length = 0; length < db::HASH_HEADER_SIZE; ++length)
      EXPECT_FALSE(db::encode_hash_header(decoded, 123, storage + 1, length));
    EXPECT_FALSE(
        db::encode_hash_header(decoded, 123, nullptr, db::HASH_HEADER_SIZE));
    decoded.fill_factor = 0;
    EXPECT_FALSE(db::encode_hash_header(decoded, 123, storage + 1,
                                        db::HASH_HEADER_SIZE));
    for (size_t i = 0; i < sizeof(reference); ++i)
      EXPECT_EQ(storage[1 + i], reference[i]);
  }
}

TEST(LlvmLibcHashValidationTest, EmptyAndOrdinaryPages) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    unsigned char bytes[db::HASH_PAGE_SIZE] = {};
    EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), be) ==
                Status::ZeroFilled);
    half(bytes, 1, 4090, be);
    half(bytes, 2, 4096, be);
    EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), be) ==
                Status::Valid);
    // Two adjacent records, including an empty content field.
    half(bytes, 0, 4, be);
    half(bytes, 1, 4094, be);
    half(bytes, 2, 4090, be);
    half(bytes, 3, 4089, be);
    half(bytes, 4, 4089, be);
    half(bytes, 5, 4089 - 14, be);
    half(bytes, 6, 4089, be);
    EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), be) ==
                Status::Valid);
    // A terminal overflow link consumes metadata, not payload.
    half(bytes, 0, 6, be);
    half(bytes, 5, 2050, be);
    half(bytes, 6, 0, be);
    half(bytes, 7, 4089 - 18, be);
    half(bytes, 8, 4089, be);
    EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), be) ==
                Status::Valid);
  }
}

TEST(LlvmLibcHashValidationTest, MalformedPagesAndLargeRecords) {
  unsigned char bytes[db::HASH_PAGE_SIZE] = {};
  EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes) - 1, true) ==
              Status::Malformed);
  EXPECT_TRUE(db::validate_hash_page(nullptr, sizeof(bytes), true) ==
              Status::Malformed);
  half(bytes, 0, 65534, true);
  EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), true) ==
              Status::Malformed);
  half(bytes, 0, 3, true);
  EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), true) ==
              Status::Malformed);
  half(bytes, 0, 2, true);
  half(bytes, 1, 4090, true);
  half(bytes, 2, 4080, true);
  half(bytes, 3, 4070, true);
  half(bytes, 4, 4080, true);
  EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), true) ==
              Status::Valid);
  half(bytes, 3, 4069, true);
  EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), true) ==
              Status::Malformed);
  half(bytes, 3, 4070, true);
  half(bytes, 1, 4079, true);
  EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), true) ==
              Status::Malformed);
  half(bytes, 1, 4090, true);
  half(bytes, 2, 1, true);
  EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), true) ==
              Status::LargeRecord);
  half(bytes, 2, 0, true);
  half(bytes, 1, 2048, true);
  EXPECT_TRUE(db::validate_hash_page(bytes, sizeof(bytes), true) ==
              Status::Malformed);
}

TEST(LlvmLibcHashValidationTest, RecordLimit) {
  EXPECT_TRUE(db::hash_record_fits(0, 0));
  EXPECT_TRUE(db::hash_record_fits(512, 511));
  EXPECT_TRUE(db::hash_record_fits(2041, 2041));
  EXPECT_TRUE(db::hash_record_fits(4082, 0));
  EXPECT_TRUE(db::hash_record_fits(0, 4082));
  EXPECT_FALSE(db::hash_record_fits(4082, 1));
  EXPECT_FALSE(db::hash_record_fits(size_t(-1), 1));
  EXPECT_FALSE(db::hash_record_fits(1, size_t(-1)));
}

TEST(LlvmLibcHashValidationTest, LogicalPageAddresses) {
  unsigned char bytes[db::HASH_HEADER_SIZE];
  header(bytes);
  db::HashHeader h{};
  ASSERT_TRUE(db::decode_hash_header(bytes, sizeof(bytes), 123, h));
  uint32_t physical = 999;
  ASSERT_TRUE(db::hash_bucket_page(h, 0, physical));
  EXPECT_EQ(physical, uint32_t(1));
  ASSERT_TRUE(db::hash_bucket_page(h, 1, physical));
  EXPECT_EQ(physical, uint32_t(2));
  ASSERT_TRUE(db::hash_overflow_page(h, 2049, physical));
  EXPECT_EQ(physical, uint32_t(3));
  ASSERT_TRUE(db::hash_overflow_page(h, 2050, physical));
  EXPECT_EQ(physical, uint32_t(4));
  EXPECT_FALSE(db::hash_overflow_page(h, 2051, physical));
  EXPECT_FALSE(db::hash_overflow_page(h, 4097, physical));
  EXPECT_FALSE(db::hash_bucket_page(h, 2, physical));
  EXPECT_EQ(physical, uint32_t(4));
  // A split adds buckets after the preceding split's spare pages.
  h.max_bucket = 3;
  h.overflow_point = 2;
  h.spares[2] = 5;
  ASSERT_TRUE(db::hash_bucket_page(h, 2, physical));
  EXPECT_EQ(physical, uint32_t(5));
  ASSERT_TRUE(db::hash_bucket_page(h, 3, physical));
  EXPECT_EQ(physical, uint32_t(6));
  ASSERT_TRUE(db::hash_overflow_page(h, 4097, physical));
  EXPECT_EQ(physical, uint32_t(7));
  ASSERT_TRUE(db::hash_overflow_page(h, 4099, physical));
  EXPECT_EQ(physical, uint32_t(9));
  EXPECT_FALSE(db::hash_overflow_page(h, 4100, physical));
  EXPECT_EQ(physical, uint32_t(9));
}

TEST(LlvmLibcHashValidationTest, RejectImpossibleAddressGeometry) {
  unsigned char bytes[db::HASH_HEADER_SIZE];
  db::HashHeader h{};
  header(bytes);
  half(bytes + 196, 0, 2051, true);
  EXPECT_FALSE(db::decode_hash_header(bytes, sizeof(bytes), 123, h));
  header(bytes);
  half(bytes + 196, 0, 4097, true);
  EXPECT_FALSE(db::decode_hash_header(bytes, sizeof(bytes), 123, h));
  header(bytes);
  word(bytes, 18, 2048);
  EXPECT_FALSE(db::decode_hash_header(bytes, sizeof(bytes), 123, h));
  header(bytes);
  word(bytes, 10, 3);
  EXPECT_FALSE(db::decode_hash_header(bytes, sizeof(bytes), 123, h));
  header(bytes);
  word(bytes, 5, 0x800000);
  word(bytes, 8, 30);
  word(bytes, 10, 0x7fffffff);
  word(bytes, 11, 0x7fffffff);
  word(bytes, 12, 0x3fffffff);
  EXPECT_FALSE(db::decode_hash_header(bytes, sizeof(bytes), 123, h));
}
