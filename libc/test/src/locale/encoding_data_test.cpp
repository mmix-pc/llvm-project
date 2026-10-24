//===-- ISO-2022-CN mapping validation tests
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/encoding_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::ChineseEncodingData;
using LIBC_NAMESPACE::internal::LocaleDataView;

namespace {
struct Fixture {
  uint8_t storage[512]{};
  uint8_t *bytes = storage + 1;
  size_t size;
  size_t offsets[6];
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - i * 8));
  }
  Fixture() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    size_t end = 96;
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *entry = bytes + 130 + i * 16;
      put(entry, 0x201 + i);
      put(entry + 4, 2);
      put(entry + 8, uint32_t(end));
      put(entry + 12, i % 2 ? 8 : 16);
      uint8_t *data = bytes + 130 + end;
      offsets[i] = 130 + end;
      if (i % 2) {
        put(data, 0x4e2d);
        put(data + 4, 0x2121);
        end += 8;
      } else {
        put(data, 0x2121);
        put(data + 4, 0x4e2d);
        put(data + 8, 0x2122);
        put(data + 12, 0x4e2d);
        end += 16;
      }
    }
    size = 130 + end;
    put(bytes + 16, uint32_t(size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *directory = bytes + 32 + i * 16;
      put(directory, i);
      put(directory + 4, i ? uint32_t(size) : 130);
      put(directory + 8, i ? 0 : uint32_t(end));
      put(directory + 12, i ? 0 : 6);
    }
  }
  bool accepted() {
    auto file = LocaleDataView::parse({bytes, size});
    return file && ChineseEncodingData::read(*file).has_value();
  }
};
} // namespace

TEST(LlvmLibcEncodingDataTest, DirectionalLookups) {
  Fixture f;
  auto file = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(file.has_value());
  auto data = ChineseEncodingData::read(*file);
  ASSERT_TRUE(data.has_value());
  for (unsigned i = 0; i != 3; ++i) {
    auto set = ChineseEncodingData::Set(i);
    EXPECT_EQ(data->decode(set, 0x2121).value(), uint32_t(0x4e2d));
    EXPECT_EQ(data->decode(set, 0x2122).value(), uint32_t(0x4e2d));
    EXPECT_EQ(data->encode(set, 0x4e2d).value(), uint32_t(0x2121));
    EXPECT_FALSE(data->decode(set, 0x2021).has_value());
    EXPECT_FALSE(data->decode(set, 0x2123).has_value());
    EXPECT_FALSE(data->encode(set, 0x4e2e).has_value());
    EXPECT_FALSE(data->encode(set, 0xd800).has_value());
  }
  EXPECT_FALSE(
      data->encode(ChineseEncodingData::SET_COUNT, 0x4e2d).has_value());
  EXPECT_FALSE(
      data->decode(ChineseEncodingData::SET_COUNT, 0x2121).has_value());
}

TEST(LlvmLibcEncodingDataTest, MissingAndMistypedTables) {
  for (unsigned i = 0; i != 6; ++i) {
    Fixture f;
    Fixture::put(f.bytes + 32 + 12, i);
    EXPECT_FALSE(f.accepted());
    Fixture wrong;
    Fixture::put(wrong.bytes + 130 + i * 16 + 4, 3);
    EXPECT_FALSE(wrong.accepted());
  }
}

TEST(LlvmLibcEncodingDataTest, RejectBadCoordinatesAndScalars) {
  const uint32_t positions[] = {0, 0x2021, 0x217f, 0x7f21, 0x12121};
  for (uint32_t position : positions) {
    Fixture f;
    Fixture::put(f.bytes + f.offsets[0], position);
    EXPECT_FALSE(f.accepted());
  }
  const uint32_t scalars[] = {0xd800, 0xdfff, 0x110000, uint32_t(-1)};
  for (uint32_t scalar : scalars) {
    Fixture f;
    Fixture::put(f.bytes + f.offsets[0] + 4, scalar);
    EXPECT_FALSE(f.accepted());
  }
}

TEST(LlvmLibcEncodingDataTest, RejectOrderingAndNonRoundtripEncoder) {
  Fixture duplicate;
  Fixture::put(duplicate.bytes + duplicate.offsets[0] + 8, 0x2121);
  EXPECT_FALSE(duplicate.accepted());
  Fixture reversed;
  Fixture::put(reversed.bytes + reversed.offsets[0], 0x2123);
  EXPECT_FALSE(reversed.accepted());
  Fixture mismatch;
  Fixture::put(mismatch.bytes + mismatch.offsets[1], 0x4e2e);
  EXPECT_FALSE(mismatch.accepted());
  Fixture absent;
  Fixture::put(absent.bytes + absent.offsets[1] + 4, 0x2123);
  EXPECT_FALSE(absent.accepted());
  Fixture truncated;
  Fixture::put(truncated.bytes + 130 + 12, 12);
  EXPECT_FALSE(truncated.accepted());
}
