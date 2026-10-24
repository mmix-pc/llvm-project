//===-- Message category text tests ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/langinfo_macros.h"
#include "src/locale/messages_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::LocaleDataView;
using LIBC_NAMESPACE::internal::MessagesLocaleData;

namespace {
struct Fixture {
  uint8_t storage[512]{};
  uint8_t *bytes = storage + 1;
  size_t size;
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - i * 8));
  }
  Fixture(const char *const strings[4]) {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    size_t end = 64;
    for (unsigned i = 0; i != 4; ++i) {
      size_t length = 1;
      while (strings[i][length - 1])
        ++length;
      uint8_t *record = bytes + 130 + i * 16;
      put(record, i + 1);
      put(record + 4, 1);
      put(record + 8, uint32_t(end));
      put(record + 12, uint32_t(length));
      for (size_t j = 0; j != length; ++j)
        bytes[130 + end + j] = uint8_t(strings[i][j]);
      end += length;
    }
    size = 130 + end;
    put(bytes + 16, uint32_t(size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *directory = bytes + 32 + i * 16;
      put(directory, i);
      put(directory + 4, 130);
      put(directory + 8, i == 5 ? uint32_t(end) : 0);
      put(directory + 12, i == 5 ? 4 : 0);
    }
  }
  bool accepted() {
    auto view = LocaleDataView::parse({bytes, size});
    return view && MessagesLocaleData::read(*view).has_value();
  }
};
} // namespace

TEST(LlvmLibcMessagesDataTest, UnalignedUtf8Views) {
  const char *strings[] = {"^[eE]", "^[hH]", "evet", "hay\xc4\xb1r"};
  Fixture f(strings);
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto data = MessagesLocaleData::read(*view);
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->yesexpr.bytes.size(), size_t(6));
  EXPECT_EQ(data->noexpr.bytes[2], uint8_t('h'));
  EXPECT_EQ(data->yesstr.bytes.size(), size_t(5));
  EXPECT_EQ(data->nostr.bytes.size(), size_t(7));
  EXPECT_EQ(data->nostr.bytes[3], uint8_t(0xc4));
}

TEST(LlvmLibcMessagesDataTest, EmptyAndUninterpretedExpressions) {
  const char *strings[] = {"[", "", "", ""};
  Fixture f(strings);
  // This layer checks text, not whether '[' is a valid regular expression.
  EXPECT_TRUE(f.accepted());
}

TEST(LlvmLibcMessagesDataTest, MissingAndMistypedFields) {
  const char *strings[] = {"yes", "no", "yes", "no"};
  for (unsigned i = 0; i != 4; ++i) {
    Fixture wrong(strings);
    Fixture::put(wrong.bytes + 130 + i * 16 + 4, 3);
    EXPECT_FALSE(wrong.accepted());
    Fixture missing(strings);
    Fixture::put(missing.bytes + 32 + 5 * 16 + 12, i);
    EXPECT_FALSE(missing.accepted());
  }
}

TEST(LlvmLibcMessagesDataTest, RejectInvalidUtf8) {
  const char *bad[] = {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80",
                       "\xe2\x82"};
  for (const char *text : bad) {
    for (unsigned i = 0; i != 4; ++i) {
      const char *strings[] = {"yes", "no", "", ""};
      strings[i] = text;
      Fixture f(strings);
      EXPECT_FALSE(f.accepted());
    }
  }
}
