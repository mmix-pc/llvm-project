//===-- Time category text table tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/langinfo_macros.h"
#include "src/locale/time_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::LocaleDataView;
using LIBC_NAMESPACE::internal::TimeLocaleData;

namespace {
struct Fixture {
  static constexpr unsigned COUNTS[] = {7, 7, 12, 12, 12, 12, 2, 1,
                                        1, 1, 1,  0,  1,  1,  1, 0};
  uint8_t storage[4096]{};
  uint8_t *bytes = storage + 1;
  size_t size = 0;
  size_t count_offsets[16]{};
  const char *names = "\xc5\x9e";
  unsigned wrong_kind = 16;
  unsigned omit_field = 16;
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - i * 8));
  }
  void build() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    unsigned entries = 0;
    for (unsigned field = 0; field != 16; ++field)
      if (field != omit_field)
        entries += 1 + COUNTS[field];
    size_t table = 130;
    size_t payload = table + entries * 16;
    auto item = [&](uint32_t key, unsigned kind, size_t length) {
      put(bytes + table, key);
      put(bytes + table + 4, kind);
      put(bytes + table + 8, uint32_t(payload - 130));
      put(bytes + table + 12, uint32_t(length));
      table += 16;
    };
    for (unsigned field = 0; field != 16; ++field) {
      if (field == omit_field)
        continue;
      uint32_t key = (field + 1) << 16;
      item(key, field == wrong_kind ? 3 : 2, 4);
      count_offsets[field] = payload;
      put(bytes + payload, COUNTS[field]);
      payload += 4;
      for (unsigned i = 0; i != COUNTS[field]; ++i) {
        const char *text = field < 6 ? names : "";
        size_t length = 1;
        while (text[length - 1])
          ++length;
        item(key + i + 1, 1, length);
        for (size_t j = 0; j != length; ++j)
          bytes[payload + j] = uint8_t(text[j]);
        payload += length;
      }
    }
    size = payload;
    put(bytes + 16, uint32_t(size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *directory = bytes + 32 + i * 16;
      put(directory, i);
      put(directory + 4, i <= 2 ? 130 : uint32_t(size));
      put(directory + 8, i == 2 ? uint32_t(size - 130) : 0);
      put(directory + 12, i == 2 ? entries : 0);
    }
  }
  bool accepted() {
    auto file = LocaleDataView::parse({bytes, size});
    return file && TimeLocaleData::read(*file).has_value();
  }
};
} // namespace

TEST(LlvmLibcTimeDataTest, TablesAndEmptyValues) {
  Fixture f;
  f.build();
  auto file = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(file.has_value());
  auto time = TimeLocaleData::read(*file);
  ASSERT_TRUE(time.has_value());
  EXPECT_EQ(time->count(TimeLocaleData::DAY), size_t(7));
  EXPECT_EQ(time->count(TimeLocaleData::ALT_MON), size_t(12));
  EXPECT_EQ(time->count(TimeLocaleData::ERAS), size_t(0));
  auto name = time->text(TimeLocaleData::MON, 11);
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(name->bytes.size(), size_t(3));
  EXPECT_EQ(name->bytes[0], uint8_t(0xc5));
  auto am = time->text(TimeLocaleData::AM_PM, 1);
  ASSERT_TRUE(am.has_value());
  EXPECT_EQ(am->bytes.size(), size_t(1));
  EXPECT_FALSE(time->text(TimeLocaleData::MON, 12).has_value());
  EXPECT_FALSE(time->text(TimeLocaleData::ERAS, 0).has_value());
  EXPECT_FALSE(time->text(TimeLocaleData::FIELD_COUNT, 0).has_value());
  EXPECT_EQ(time->count(TimeLocaleData::FIELD_COUNT), size_t(0));
}

TEST(LlvmLibcTimeDataTest, RejectMissingOrMistypedCount) {
  for (unsigned field = 0; field != 16; ++field) {
    Fixture f;
    f.omit_field = field;
    f.build();
    EXPECT_FALSE(f.accepted());
    Fixture wrong;
    wrong.wrong_kind = field;
    wrong.build();
    EXPECT_FALSE(wrong.accepted());
  }
}

TEST(LlvmLibcTimeDataTest, RejectInvalidCardinality) {
  for (unsigned field = 0; field != 16; ++field) {
    Fixture f;
    f.build();
    Fixture::put(f.bytes + f.count_offsets[field], uint32_t(-1));
    EXPECT_FALSE(f.accepted());
    Fixture::put(f.bytes + f.count_offsets[field], Fixture::COUNTS[field] + 1);
    EXPECT_FALSE(f.accepted());
  }
}

TEST(LlvmLibcTimeDataTest, RejectInvalidOrEmptyNames) {
  const char *bad[] = {"", "\xc0\x80", "\xed\xa0\x80", "\xe2\x82"};
  for (const char *name : bad) {
    Fixture f;
    f.names = name;
    f.build();
    EXPECT_FALSE(f.accepted());
  }
}
