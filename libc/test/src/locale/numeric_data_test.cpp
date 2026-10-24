//===-- Numeric locale data tests ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/locale/numeric_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::LocaleDataView;
using LIBC_NAMESPACE::internal::NumericLocaleData;

namespace {
void put(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i != 4; ++i)
    p[i] = uint8_t(value >> (24 - i * 8));
}

struct Fixture {
  uint8_t storage[512]{};
  uint8_t *bytes = storage + 1;
  size_t size;
  Fixture(const char *decimal, const char *separator, const uint8_t *grouping,
          size_t grouping_size) {
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
    size_t end = 48;
    const char *strings[] = {decimal, separator};
    for (unsigned i = 0; i != 3; ++i) {
      size_t count = grouping_size;
      if (i < 2) {
        count = 1;
        while (strings[i][count - 1])
          ++count;
      }
      put(section + i * 16, i + 1);
      put(section + i * 16 + 4, i == 2 ? 3 : 1);
      put(section + i * 16 + 8, uint32_t(end));
      put(section + i * 16 + 12, uint32_t(count));
      for (size_t j = 0; j != count; ++j)
        section[end + j] = i == 2 ? grouping[j] : uint8_t(strings[i][j]);
      end += count;
    }
    size = 130 + end;
    put(bytes + 16, uint32_t(size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *directory = bytes + 32 + i * 16;
      put(directory, i);
      put(directory + 4, uint32_t(i <= 1 ? 130 : size));
      put(directory + 8, i == 1 ? uint32_t(end) : 0);
      put(directory + 12, i == 1 ? 3 : 0);
    }
  }
  bool accepted() {
    auto file = LocaleDataView::parse({bytes, size});
    return file && NumericLocaleData::read(*file).has_value();
  }
};
} // namespace

TEST(LlvmLibcNumericDataTest, ValidCharactersAndGrouping) {
  const uint8_t repeat[] = {3, 2, 0};
  Fixture f("\xd9\xab", "\xe2\x80\xaf", repeat, sizeof(repeat));
  auto file = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(file.has_value());
  auto numeric = NumericLocaleData::read(*file);
  ASSERT_TRUE(numeric.has_value());
  EXPECT_EQ(numeric->decimal_point.bytes.size(), size_t(3));
  EXPECT_EQ(numeric->thousands_sep.bytes.size(), size_t(4));
  EXPECT_EQ(numeric->grouping.bytes[1], uint8_t(2));
  const uint8_t stop[] = {3, 127, 0};
  Fixture stopped(".", ",", stop, sizeof(stop));
  EXPECT_TRUE(stopped.accepted());
  const uint8_t none[] = {0};
  Fixture c(".", "", none, sizeof(none));
  EXPECT_TRUE(c.accepted());
}

TEST(LlvmLibcNumericDataTest, InvalidUnicodeAndCardinality) {
  const uint8_t grouping[] = {0};
  const char *invalid[] = {
      "ab",   "\xc0\x80", "\xed\xa0\x80",        "\xf4\x90\x80\x80",
      "\x80", "\xe2\x80", "\xf8\x88\x80\x80\x80"};
  for (const char *text : invalid) {
    Fixture decimal(text, "", grouping, sizeof(grouping));
    EXPECT_FALSE(decimal.accepted());
    Fixture separator(".", text, grouping, sizeof(grouping));
    EXPECT_FALSE(separator.accepted());
  }
  Fixture empty("", "", grouping, sizeof(grouping));
  EXPECT_FALSE(empty.accepted());
}

TEST(LlvmLibcNumericDataTest, InvalidGrouping) {
  const uint8_t invalid[][3] = {{0, 3, 0}, {127, 3, 0}, {128, 3, 0}, {3, 3, 1}};
  for (const auto &grouping : invalid) {
    Fixture f(".", ",", grouping, sizeof(grouping));
    EXPECT_FALSE(f.accepted());
  }
  Fixture empty(".", ",", nullptr, 0);
  EXPECT_FALSE(empty.accepted());
}

TEST(LlvmLibcNumericDataTest, MissingAndMistypedFields) {
  const uint8_t grouping[] = {3, 0};
  for (unsigned i = 0; i != 3; ++i) {
    Fixture missing(".", ",", grouping, sizeof(grouping));
    put(missing.bytes + 48 + 12, i);
    EXPECT_FALSE(missing.accepted());
    Fixture wrong(".", ",", grouping, sizeof(grouping));
    put(wrong.bytes + 130 + i * 16 + 4, i == 2 ? 1 : 3);
    EXPECT_FALSE(wrong.accepted());
  }
}
