//===-- Character category data tests -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/ctype_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::CTypeLocaleData;
using LIBC_NAMESPACE::internal::LocaleDataView;

namespace {
struct Fixture {
  uint32_t words[14][16]{};
  size_t counts[14]{};
  uint8_t storage[2048]{};
  uint8_t *bytes = storage + 1;
  size_t size;

  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - i * 8));
  }
  Fixture() {
    const unsigned fields[] = {0, 2, 7, 8};
    for (unsigned field : fields) {
      words[field][0] = words[field][1] = 65;
      counts[field] = 2;
    }
    words[11][0] = 97;
    words[11][1] = 65;
    counts[11] = 2;
  }
  void build() {
    const uint8_t magic[] = {'L', 'L', 'V', 'M', 'L', 'C', 'L', 0};
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = magic[i];
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    uint8_t *section = bytes + 130;
    size_t offset = 14 * 16;
    for (unsigned i = 0; i != 14; ++i) {
      put(section + i * 16, i < 11 ? i + 1 : 0x101 + i - 11);
      put(section + i * 16 + 4, 2);
      put(section + i * 16 + 8, uint32_t(offset));
      put(section + i * 16 + 12, uint32_t(counts[i] * 4));
      for (size_t j = 0; j != counts[i]; ++j)
        put(section + offset + j * 4, words[i][j]);
      offset += counts[i] * 4;
    }
    size = 130 + offset;
    put(bytes + 16, uint32_t(size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *d = bytes + 32 + i * 16;
      put(d, i);
      put(d + 4, i ? uint32_t(size) : 130);
      put(d + 8, i ? 0 : uint32_t(offset));
      put(d + 12, i ? 0 : 14);
    }
  }
  bool accepted() {
    build();
    auto view = LocaleDataView::parse({bytes, size});
    return view && CTypeLocaleData::read(*view).has_value();
  }
};
} // namespace

TEST(LlvmLibcCTypeDataTest, LookupAndIdentity) {
  Fixture f;
  f.build();
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto ctype = CTypeLocaleData::read(*view);
  ASSERT_TRUE(ctype.has_value());
  EXPECT_TRUE(ctype->contains(CTypeLocaleData::UPPER, 65));
  EXPECT_FALSE(ctype->contains(CTypeLocaleData::UPPER, 64));
  EXPECT_FALSE(ctype->contains(CTypeLocaleData::UPPER, 66));
  EXPECT_FALSE(ctype->contains(CTypeLocaleData::UPPER, 0xd800));
  EXPECT_FALSE(ctype->contains(CTypeLocaleData::Class(99), 65));
  EXPECT_EQ(ctype->map(CTypeLocaleData::TOUPPER, 97), uint32_t(65));
  EXPECT_EQ(ctype->map(CTypeLocaleData::TOUPPER, 98), uint32_t(98));
  EXPECT_EQ(ctype->map(CTypeLocaleData::Mapping(99), 97), uint32_t(97));
  EXPECT_EQ(ctype->map(CTypeLocaleData::TOUPPER, 0xffffffff),
            uint32_t(0xffffffff));
}

TEST(LlvmLibcCTypeDataTest, InvalidTables) {
  const uint32_t invalid[] = {0xd800, 0x110000, uint32_t(-1)};
  for (uint32_t bad : invalid) {
    Fixture ranges;
    ranges.words[0][0] = ranges.words[0][1] = bad;
    EXPECT_FALSE(ranges.accepted());
    Fixture mapping;
    mapping.words[11][1] = bad;
    EXPECT_FALSE(mapping.accepted());
  }
  Fixture cross_surrogates;
  cross_surrogates.words[0][0] = 0xd7ff;
  cross_surrogates.words[0][1] = 0xe000;
  EXPECT_FALSE(cross_surrogates.accepted());
  Fixture reversed;
  reversed.words[0][1] = 64;
  EXPECT_FALSE(reversed.accepted());
  Fixture duplicate;
  duplicate.words[11][2] = 97;
  duplicate.words[11][3] = 66;
  duplicate.counts[11] = 4;
  EXPECT_FALSE(duplicate.accepted());
  Fixture half_pair;
  half_pair.counts[11] = 1;
  EXPECT_FALSE(half_pair.accepted());
}

TEST(LlvmLibcCTypeDataTest, ClassRelationships) {
  Fixture missing_alpha;
  missing_alpha.counts[2] = 0;
  EXPECT_FALSE(missing_alpha.accepted());
  Fixture overlap;
  overlap.words[4][0] = overlap.words[4][1] = 65;
  overlap.counts[4] = 2;
  EXPECT_FALSE(overlap.accepted());
  Fixture adjacent;
  adjacent.words[0][1] = adjacent.words[2][1] = adjacent.words[7][1] = 66;
  adjacent.words[8][2] = adjacent.words[8][3] = 66;
  adjacent.counts[8] = 4;
  EXPECT_TRUE(adjacent.accepted());
  adjacent.words[8][2] = adjacent.words[8][3] = 67;
  EXPECT_FALSE(adjacent.accepted());
}

TEST(LlvmLibcCTypeDataTest, MissingFields) {
  Fixture f;
  f.build();
  Fixture::put(f.bytes + 44, 13);
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  EXPECT_FALSE(CTypeLocaleData::read(*view).has_value());
}
