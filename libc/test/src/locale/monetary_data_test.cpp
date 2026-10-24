//===-- Monetary category data tests --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/monetary_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::LocaleDataView;
using LIBC_NAMESPACE::internal::MonetaryLocaleData;

namespace {
struct Fixture {
  const char *strings[6] = {"USD ", "$", ".", ",", "", "-"};
  uint32_t numbers[14] = {2, 2, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1};
  uint8_t storage[2048]{};
  uint8_t *bytes = storage + 1;
  size_t size;
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - i * 8));
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
    size_t offset = 21 * 16;
    for (unsigned i = 0; i != 21; ++i) {
      size_t length = i == 6 ? 2 : 4;
      if (i < 6) {
        length = 1;
        while (strings[i][length - 1])
          ++length;
        for (size_t j = 0; j != length; ++j)
          section[offset + j] = uint8_t(strings[i][j]);
      } else if (i == 6) {
        section[offset] = 3;
        section[offset + 1] = 0;
      } else {
        put(section + offset, numbers[i - 7]);
      }
      put(section + i * 16, i + 1);
      put(section + i * 16 + 4, i < 6 ? 1 : i == 6 ? 3 : 2);
      put(section + i * 16 + 8, uint32_t(offset));
      put(section + i * 16 + 12, uint32_t(length));
      offset += length;
    }
    size = 130 + offset;
    put(bytes + 16, uint32_t(size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *d = bytes + 32 + i * 16;
      put(d, i);
      put(d + 4, i <= 4 ? 130 : uint32_t(size));
      put(d + 8, i == 4 ? uint32_t(offset) : 0);
      put(d + 12, i == 4 ? 21 : 0);
    }
  }
  bool accepted() {
    build();
    auto view = LocaleDataView::parse({bytes, size});
    return view && MonetaryLocaleData::read(*view).has_value();
  }
};
} // namespace

TEST(LlvmLibcMonetaryDataTest, LookupAndMultibyteStrings) {
  Fixture f;
  f.strings[0] = "TRY ";
  f.strings[1] = "\xe2\x82\xba";
  f.build();
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto data = MonetaryLocaleData::read(*view);
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->strings[MonetaryLocaleData::CURRENCY_SYMBOL].bytes.size(),
            size_t(4));
  EXPECT_EQ(data->numbers[MonetaryLocaleData::INT_FRAC_DIGITS], uint8_t(2));
  EXPECT_EQ(data->numbers[MonetaryLocaleData::P_SEP_BY_SPACE], uint8_t(0));
  EXPECT_EQ(data->numbers[MonetaryLocaleData::INT_P_SEP_BY_SPACE], uint8_t(1));
  for (auto &number : f.numbers)
    number = 127;
  for (auto &string : f.strings)
    string = "";
  EXPECT_TRUE(f.accepted());
}

TEST(LlvmLibcMonetaryDataTest, RejectStrings) {
  const char *invalid[] = {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80",
                           "\xe2\x82"};
  for (const char *text : invalid) {
    Fixture f;
    f.strings[1] = text;
    EXPECT_FALSE(f.accepted());
  }
  Fixture separator;
  separator.strings[2] = "..";
  EXPECT_FALSE(separator.accepted());
  Fixture symbol;
  symbol.strings[0] = "USD";
  EXPECT_FALSE(symbol.accepted());
  symbol.strings[0] = "usd ";
  EXPECT_FALSE(symbol.accepted());
}

TEST(LlvmLibcMonetaryDataTest, RejectInvalidNumbers) {
  const uint32_t limits[] = {126, 126, 1, 2, 1, 2, 4, 4, 1, 2, 1, 2, 4, 4};
  for (unsigned i = 0; i != 14; ++i) {
    Fixture f;
    f.numbers[i] = limits[i] == 126 ? 128 : limits[i] + 1;
    EXPECT_FALSE(f.accepted());
    f.numbers[i] = uint32_t(-1);
    EXPECT_FALSE(f.accepted());
  }
}

TEST(LlvmLibcMonetaryDataTest, RejectMissingAndMistypedItems) {
  Fixture missing;
  missing.build();
  Fixture::put(missing.bytes + 32 + 4 * 16 + 12, 20);
  auto view = LocaleDataView::parse({missing.bytes, missing.size});
  ASSERT_TRUE(view.has_value());
  EXPECT_FALSE(MonetaryLocaleData::read(*view).has_value());
  Fixture wrong;
  wrong.build();
  Fixture::put(wrong.bytes + 130 + 7 * 16 + 4, 3);
  view = LocaleDataView::parse({wrong.bytes, wrong.size});
  ASSERT_TRUE(view.has_value());
  EXPECT_FALSE(MonetaryLocaleData::read(*view).has_value());
}
