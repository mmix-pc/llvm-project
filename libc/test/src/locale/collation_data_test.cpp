//===-- Collation data tests ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/locale/collation_data.h"
#include "src/locale/collation_key.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::CollationData;
using LIBC_NAMESPACE::internal::compare_collation;
using LIBC_NAMESPACE::internal::LocaleDataView;
using LIBC_NAMESPACE::internal::resolve_collation;
using LIBC_NAMESPACE::internal::transform_collation;

namespace {
struct Fixture {
  uint8_t storage[1025]{};
  uint8_t *bytes = storage + 1;
  uint32_t records[4][32] = {{3, 7, 1, 1, 1, 1, 5},
                             {1, 97, 1, 2, 1, 5, 1, 3, 1, 1, 0, 1, 5},
                             {2, 97, 98, 1, 2, 1, 5, 1, 4, 1, 2, 0, 1, 6},
                             {1, 98, 1, 2, 1, 5, 2, 3, 4, 1, 1, 0, 1, 7}};
  size_t counts[4] = {7, 13, 14, 14};
  size_t size = 0;
  static void word(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - 8 * i));
  }
  void build() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    word(bytes + 8, 1);
    word(bytes + 12, 0x01020304);
    word(bytes + 20, 6);
    word(bytes + 24, 128);
    word(bytes + 28, 2);
    bytes[128] = 'x';
    size_t offset = 64;
    for (unsigned i = 0; i != 4; ++i) {
      auto *entry = bytes + 130 + 16 * i;
      word(entry, i ? 0x1000 + i - 1 : 1);
      word(entry + 4, 2);
      word(entry + 8, uint32_t(offset));
      word(entry + 12, uint32_t(counts[i] * 4));
      for (size_t j = 0; j != counts[i]; ++j)
        word(bytes + 130 + offset + 4 * j, records[i][j]);
      offset += counts[i] * 4;
    }
    size = 130 + offset;
    word(bytes + 16, uint32_t(size));
    for (unsigned i = 0; i != 6; ++i) {
      auto *directory = bytes + 32 + 16 * i;
      word(directory, i);
      word(directory + 4, i <= 3 ? 130 : uint32_t(size));
      word(directory + 8, i == 3 ? uint32_t(offset) : 0);
      word(directory + 12, i == 3 ? 4 : 0);
    }
  }
  LIBC_NAMESPACE::ErrorOr<CollationData> read() {
    build();
    auto view = LocaleDataView::parse({bytes, size});
    if (!view)
      return LIBC_NAMESPACE::Error(view.error());
    return CollationData::read(*view);
  }
};
} // namespace

TEST(LlvmLibcCollationDataTest, ContractionsExpansionsAndLongestMatch) {
  Fixture f;
  auto data = f.read();
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->size(), size_t(3));
  const uint32_t a[] = {97}, ab[] = {97, 98}, abc[] = {97, 98, 99},
                 ac[] = {97, 99};
  auto one = data->find(a);
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(one->direction(1).value(), uint32_t(2));
  EXPECT_EQ(one->direction(3).value(), uint32_t(5));
  EXPECT_EQ(one->weights(0)->at(0).value(), uint32_t(3));
  EXPECT_EQ(one->weights(2)->size(), size_t(0));
  EXPECT_EQ(data->find(ab)->source_size(), size_t(2));
  EXPECT_EQ(data->longest(abc)->source_size(), size_t(2));
  EXPECT_EQ(data->longest(ac)->source_size(), size_t(1));
  EXPECT_EQ(data->rule(2)->weights(0)->size(), size_t(2));
  EXPECT_EQ(data->rule(2)->weights(0)->at(1).value(), uint32_t(4));
  EXPECT_EQ(one->direction(4).error(), EINVAL);
  EXPECT_EQ(one->weights(4).error(), EINVAL);
  EXPECT_EQ(data->rule(3).error(), EINVAL);
  EXPECT_EQ(data->find(abc).error(), ENOENT);
  const uint32_t c[] = {99}, bad[] = {97, 0xd800};
  EXPECT_EQ(data->longest(c).error(), ENOENT);
  EXPECT_EQ(data->find(bad).error(), EINVAL);
  EXPECT_EQ(data->longest(bad).error(), EINVAL);
  EXPECT_EQ(data->find({}).error(), EINVAL);
  EXPECT_EQ(data->longest({}).error(), EINVAL);
}

TEST(LlvmLibcCollationDataTest, ImplicitUndefinedAndCopiedRuleLifetime) {
  Fixture f;
  auto data = f.read();
  ASSERT_TRUE(data.has_value());
  const uint32_t unknown[] = {0x378}, next[] = {0x379}, last[] = {0x10ffff};
  auto first = data->lookup(unknown);
  ASSERT_TRUE(first.has_value());
  auto copied = *first;
  first = data->lookup(next);
  EXPECT_EQ(copied.source_size(), size_t(1));
  EXPECT_EQ(copied.source(0), unknown[0]);
  EXPECT_EQ(copied.weights(0)->at(0).value(), uint32_t(8));
  EXPECT_EQ(first->weights(0)->at(0).value(), uint32_t(8));
  EXPECT_LT(copied.weights(1)->at(0).value(), first->weights(1)->at(0).value());
  EXPECT_EQ(copied.weights(3)->at(0).value(), uint32_t(9 + 0x378));
  EXPECT_EQ(copied.direction(3).value(), uint32_t(5));
  EXPECT_EQ(copied.weights(0)->at(1).error(), EINVAL);
  EXPECT_EQ(data->lookup(last)->weights(1)->at(0).value(),
            uint32_t(9 + 0x10ffff));
  EXPECT_EQ(data->lookup({}).error(), EINVAL);
}

TEST(LlvmLibcCollationDataTest, BoundsAndPolicy) {
  for (unsigned index = 0; index != 3; ++index) {
    Fixture f;
    f.records[0][index] = 0xffffffff;
    EXPECT_FALSE(f.read().has_value());
  }
  const uint32_t invalid_lengths[] = {0U, 257U, 0xffffffffU};
  for (uint32_t value : invalid_lengths) {
    Fixture f;
    f.records[1][0] = value;
    EXPECT_FALSE(f.read().has_value());
  }
  Fixture missing;
  missing.records[0][0] = 4;
  EXPECT_FALSE(missing.read().has_value());
  Fixture expansion;
  expansion.records[1][6] = 257;
  EXPECT_FALSE(expansion.read().has_value());
  Fixture directions;
  directions.records[0][3] = 7;
  EXPECT_FALSE(directions.read().has_value());
  Fixture position;
  position.records[0][6] = 1;
  EXPECT_FALSE(position.read().has_value());
}

TEST(LlvmLibcCollationDataTest, DomainsAndOrdering) {
  const uint32_t invalid_scalars[] = {0xd800U, 0x110000U};
  for (uint32_t value : invalid_scalars) {
    Fixture f;
    f.records[1][1] = value;
    EXPECT_FALSE(f.read().has_value());
  }
  const uint32_t invalid_directions[] = {0U, 3U, 4U, 7U};
  for (uint32_t value : invalid_directions) {
    Fixture f;
    f.records[1][2] = value;
    EXPECT_FALSE(f.read().has_value());
  }
  const uint32_t invalid_weights[] = {0U, 8U, 0xffffffffU};
  for (uint32_t value : invalid_weights) {
    Fixture f;
    f.records[1][7] = value;
    EXPECT_FALSE(f.read().has_value());
  }
  Fixture duplicate;
  duplicate.records[3][1] = 97;
  EXPECT_FALSE(duplicate.read().has_value());
  Fixture unordered;
  unordered.records[1][1] = 99;
  EXPECT_FALSE(unordered.read().has_value());
  Fixture zero;
  zero.records[1][1] = 0;
  EXPECT_TRUE(zero.read().has_value());
}

TEST(LlvmLibcCollationDataTest, TruncatedAndTrailingPayload) {
  for (size_t n = 0; n != 14; ++n) {
    Fixture f;
    f.counts[3] = n;
    EXPECT_FALSE(f.read().has_value());
  }
  Fixture extra;
  extra.counts[3] = 15;
  EXPECT_FALSE(extra.read().has_value());
  Fixture kind;
  kind.build();
  Fixture::word(kind.bytes + 130 + 16 + 4, 3);
  EXPECT_FALSE(CollationData::read(
                   LocaleDataView::parse({kind.bytes, kind.size}).value())
                   .has_value());
}

TEST(LlvmLibcCollationDataTest, ResolveAndTransformAgreeWithComparison) {
  Fixture f;
  auto data = f.read();
  ASSERT_TRUE(data.has_value());
  const uint32_t inputs[][4] = {
      {},         {'a'},           {'b'},   {'a', 'b'}, {'a', 'a'},
      {'b', 'a'}, {'a', 'b', 'a'}, {0x378}, {0x379},    {0x10ffff}};
  const size_t lengths[] = {0, 1, 1, 2, 2, 2, 3, 1, 1, 1};
  CollationData::Rule elements[10][4]{};
  size_t counts[10];
  uint8_t keys[10][256]{};
  size_t sizes[10];
  for (size_t i = 0; i != 10; ++i) {
    auto count = resolve_collation(*data, {inputs[i], lengths[i]}, elements[i]);
    ASSERT_TRUE(count.has_value());
    counts[i] = *count;
    auto length = transform_collation({elements[i], counts[i]}, keys[i]);
    ASSERT_TRUE(length.has_value());
    sizes[i] = *length;
    ASSERT_LT(sizes[i], size_t(256));
    EXPECT_EQ(keys[i][sizes[i]], uint8_t(0));
    for (size_t capacity = 0; capacity <= sizes[i] + 1; ++capacity) {
      uint8_t buffer[258];
      for (auto &byte : buffer)
        byte = 0xcc;
      auto result =
          transform_collation({elements[i], counts[i]}, {buffer + 1, capacity});
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(*result, sizes[i]);
      EXPECT_EQ(buffer[0], uint8_t(0xcc));
      EXPECT_EQ(buffer[capacity + 1], uint8_t(0xcc));
      for (size_t j = 0; j != capacity; ++j)
        EXPECT_EQ(buffer[j + 1], keys[i][j]);
    }
  }
  EXPECT_EQ(counts[3], size_t(1));
  for (size_t i = 0; i != 10; ++i) {
    for (size_t j = 0; j != 10; ++j) {
      size_t k = 0;
      while (keys[i][k] && keys[i][k] == keys[j][k])
        ++k;
      int sign = (keys[i][k] > keys[j][k]) - (keys[i][k] < keys[j][k]);
      EXPECT_EQ(
          compare_collation({elements[i], counts[i]}, {elements[j], counts[j]}),
          sign);
    }
  }
  EXPECT_LT(
      compare_collation({elements[1], counts[1]}, {elements[2], counts[2]}), 0);
  EXPECT_LT(
      compare_collation({elements[2], counts[2]}, {elements[3], counts[3]}), 0);
  EXPECT_LT(
      compare_collation({elements[3], counts[3]}, {elements[7], counts[7]}), 0);
  EXPECT_LT(
      compare_collation({elements[7], counts[7]}, {elements[8], counts[8]}), 0);
  EXPECT_EQ(resolve_collation(*data, {inputs[1], 1}, {}).error(), E2BIG);
  const uint32_t invalid[] = {0xd800};
  EXPECT_EQ(resolve_collation(*data, invalid, elements[0]).error(), EILSEQ);
}

TEST(LlvmLibcCollationDataTest, BackwardSectionsAndPosition) {
  Fixture f;
  // Leave primary weights empty and compare the secondary level.
  for (unsigned i = 1; i != 4; ++i) {
    f.records[i][0] = 1;
    f.records[i][1] = 96 + i;
    const uint32_t tail[] = {1, 2, 1, 5, 0, 1, i, 0, 0};
    for (unsigned j = 0; j != 9; ++j)
      f.records[i][j + 2] = tail[j];
    f.counts[i] = 11;
  }
  auto data = f.read();
  ASSERT_TRUE(data.has_value());
  CollationData::Rule ab[] = {*data->rule(0), *data->rule(1)};
  CollationData::Rule ba[] = {*data->rule(1), *data->rule(0)};
  EXPECT_GT(compare_collation(ab, ba), 0);
  // A forward element ends the preceding backward run.
  f.records[2][3] = 1;
  data = f.read();
  ASSERT_TRUE(data.has_value());
  ab[0] = *data->rule(0);
  ab[1] = *data->rule(1);
  ba[0] = ab[1];
  ba[1] = ab[0];
  EXPECT_LT(compare_collation(ab, ba), 0);

  // Position counts ignored elements; it is not a scalar-offset comparison.
  f.records[0][4] = 5;
  for (unsigned i = 1; i != 4; ++i)
    f.records[i][3] = 5;
  f.records[1][7] = 0;
  f.records[1][8] = 0;
  f.counts[1] = 10;
  data = f.read();
  ASSERT_TRUE(data.has_value());
  ab[0] = *data->rule(0);
  ab[1] = *data->rule(1);
  ba[0] = ab[1];
  EXPECT_GT(compare_collation(ab, {ba, 1}), 0);
}

TEST(LlvmLibcCollationDataTest, WideKeysUseElementCounts) {
  Fixture f;
  auto data = f.read();
  ASSERT_TRUE(data.has_value());
  const uint32_t sources[][4] = {{},         {'a'},   {'a', 'b'},
                                 {'b', 'a'}, {0x378}, {0x10ffff}};
  const size_t lengths[] = {0, 1, 2, 2, 1, 1};
  CollationData::Rule elements[6][4]{};
  size_t counts[6];
  wchar_t keys[6][128]{};
  for (size_t i = 0; i != 6; ++i) {
    auto resolved =
        resolve_collation(*data, {sources[i], lengths[i]}, elements[i]);
    ASSERT_TRUE(resolved.has_value());
    counts[i] = *resolved;
    auto length = transform_collation({elements[i], counts[i]}, keys[i]);
    ASSERT_TRUE(length.has_value());
    ASSERT_LT(*length, size_t(128));
    EXPECT_EQ(keys[i][*length], wchar_t(0));
    uint8_t narrow[128];
    auto narrow_length = transform_collation({elements[i], counts[i]}, narrow);
    ASSERT_TRUE(narrow_length.has_value());
    EXPECT_EQ(*length, *narrow_length);
    for (size_t j = 0; j != *length; ++j) {
      EXPECT_GT(keys[i][j], wchar_t(0));
      EXPECT_EQ(keys[i][j], wchar_t(narrow[j]));
    }
    for (size_t capacity = 0; capacity <= *length + 1; ++capacity) {
      wchar_t guarded[130];
      for (auto &element : guarded)
        element = 0x5678;
      auto result = transform_collation(
          {elements[i], counts[i]},
          LIBC_NAMESPACE::cpp::span<wchar_t>(guarded + 1, capacity));
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(*result, *length);
      EXPECT_EQ(guarded[0], wchar_t(0x5678));
      EXPECT_EQ(guarded[capacity + 1], wchar_t(0x5678));
      for (size_t j = 0; j != capacity; ++j)
        EXPECT_EQ(guarded[j + 1], keys[i][j]);
    }
  }
  for (size_t i = 0; i != 6; ++i) {
    for (size_t j = 0; j != 6; ++j) {
      size_t k = 0;
      while (keys[i][k] && keys[i][k] == keys[j][k])
        ++k;
      int sign = (keys[i][k] > keys[j][k]) - (keys[i][k] < keys[j][k]);
      EXPECT_EQ(
          compare_collation({elements[i], counts[i]}, {elements[j], counts[j]}),
          sign);
    }
  }
}
