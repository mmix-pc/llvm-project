//===-- Transliteration data tests ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/wchar/encoding_converter.h"
#include "src/locale/transliteration_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::cpp::span;
using LIBC_NAMESPACE::internal::EncodingConverter;
using LIBC_NAMESPACE::internal::LocaleDataView;
using LIBC_NAMESPACE::internal::TextEncoding;
using LIBC_NAMESPACE::internal::TransliterationData;

namespace {
struct Fixture {
  uint8_t storage[1025]{};
  uint8_t *bytes = storage + 1;
  uint32_t records[4][32] = {{3},
                             {1, 97, 1, 1, 65},
                             {2, 97, 0x301, 1, 1, 0xe1},
                             {1, 0xe4, 3, 2, 97, 101, 1, 97, 0}};
  size_t counts[4] = {1, 5, 6, 9};
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
      word(entry, i ? 0x10000 + i - 1 : 0x300);
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
      word(directory + 4, i ? uint32_t(size) : 130);
      word(directory + 8, i ? 0 : uint32_t(offset));
      word(directory + 12, i ? 0 : 4);
    }
  }
  auto read() {
    build();
    return TransliterationData::read(
        LocaleDataView::parse({bytes, size}).value());
  }
};
} // namespace

TEST(LlvmLibcTransliterationDataTest, ExactSequencesAndAlternativeOrder) {
  Fixture f;
  auto data = f.read();
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->size(), size_t(3));
  const uint32_t a[] = {97}, accented[] = {97, 0x301}, umlaut[] = {0xe4};
  auto one = data->find(a);
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(one->alternative(0)->word(0).value(), uint32_t(65));
  auto two = data->find(accented);
  ASSERT_TRUE(two.has_value());
  EXPECT_EQ(two->alternative(0)->word(0).value(), uint32_t(0xe1));
  auto three = data->find(umlaut);
  ASSERT_TRUE(three.has_value());
  EXPECT_EQ(three->alternatives(), size_t(3));
  EXPECT_EQ(three->alternative(0)->bytes.size(), size_t(8));
  EXPECT_EQ(three->alternative(0)->word(1).value(), uint32_t(101));
  EXPECT_EQ(three->alternative(1)->word(0).value(), uint32_t(97));
  EXPECT_TRUE(three->alternative(2)->bytes.empty());
  EXPECT_EQ(three->alternative(3).error(), EINVAL);
  EXPECT_EQ(data->rule(3).error(), EINVAL);
  EXPECT_EQ(data->find({}).error(), EINVAL);
  const uint32_t absent[] = {98}, longer[] = {97, 0x301, 98};
  EXPECT_EQ(data->find(absent).error(), ENOENT);
  EXPECT_EQ(data->find(longer).error(), ENOENT);
}

TEST(LlvmLibcTransliterationDataTest, CountAndSequenceBounds) {
  Fixture f;
  f.records[0][0] = 65537;
  EXPECT_FALSE(f.read().has_value());
  const uint32_t sizes[] = {0U, 257U, 0xffffffffU};
  for (uint32_t size : sizes) {
    Fixture source;
    source.records[1][0] = size;
    EXPECT_FALSE(source.read().has_value());
    Fixture alternatives;
    alternatives.records[1][2] = size;
    EXPECT_FALSE(alternatives.read().has_value());
  }
  Fixture length;
  length.records[1][3] = 0xffffffff;
  EXPECT_FALSE(length.read().has_value());
  Fixture missing;
  missing.records[0][0] = 4;
  EXPECT_FALSE(missing.read().has_value());
}

TEST(LlvmLibcTransliterationDataTest, TruncatedAndTrailingPayload) {
  for (size_t n = 0; n != 9; ++n) {
    Fixture f;
    f.counts[3] = n;
    EXPECT_FALSE(f.read().has_value());
  }
  Fixture extra;
  extra.counts[3] = 10;
  EXPECT_FALSE(extra.read().has_value());
  Fixture kind;
  kind.build();
  Fixture::word(kind.bytes + 130 + 16 + 4, 3);
  EXPECT_FALSE(TransliterationData::read(
                   LocaleDataView::parse({kind.bytes, kind.size}).value())
                   .has_value());
}

TEST(LlvmLibcTransliterationDataTest, ScalarAndKeyOrderValidation) {
  const uint32_t scalars[] = {0U, 0xd800U, 0x110000U};
  for (uint32_t scalar : scalars) {
    Fixture source;
    source.records[1][1] = scalar;
    EXPECT_FALSE(source.read().has_value());
    Fixture alternative;
    alternative.records[1][4] = scalar;
    EXPECT_FALSE(alternative.read().has_value());
  }
  Fixture reversed;
  reversed.records[3][1] = 96;
  EXPECT_FALSE(reversed.read().has_value());
  Fixture duplicate;
  duplicate.records[3][1] = 97;
  EXPECT_FALSE(duplicate.read().has_value());
  Fixture empty;
  empty.records[0][0] = 0;
  auto data = empty.read();
  ASSERT_TRUE(data.has_value());
  const uint32_t key[] = {97};
  EXPECT_EQ(data->find(key).error(), ENOENT);
}

TEST(LlvmLibcTransliterationDataTest, ConversionAlternativesAndAtomicOutput) {
  Fixture fixture;
  // The first alternative has an unrepresentable combining mark. The second
  // must be tried from the original destination state, without publishing 'a'.
  const uint32_t record[] = {1, 0xe4, 2, 2, 97, 0x308, 2, 97, 101};
  for (unsigned i = 0; i != 9; ++i)
    fixture.records[3][i] = record[i];
  auto rules = fixture.read();
  ASSERT_TRUE(rules.has_value());
  auto converter =
      EncodingConverter::create(TextEncoding::UTF8, TextEncoding::ASCII,
                                EncodingConverter::NonIdentical::TRANSLIT)
          .value();
  const uint8_t umlaut[] = {0xc3, 0xa4};
  uint8_t buffer[4] = {0xcc, 0xcc, 0xcc, 0xcc};
  span<const uint8_t> in(umlaut);
  span<uint8_t> out(buffer, 1);
  EXPECT_EQ(converter.convert(in, out).error(), EINVAL);
  EXPECT_EQ(converter.convert(in, out, &*rules).error(), E2BIG);
  EXPECT_EQ(in.data(), umlaut);
  EXPECT_EQ(out.data(), buffer);
  for (auto byte : buffer)
    EXPECT_EQ(byte, uint8_t(0xcc));
  out = {buffer, 2};
  EXPECT_EQ(converter.convert(in, out, &*rules).value(), size_t(1));
  EXPECT_EQ(buffer[0], uint8_t('a'));
  EXPECT_EQ(buffer[1], uint8_t('e'));
  EXPECT_TRUE(in.empty());
  EXPECT_TRUE(out.empty());
  // Different immutable rules can be selected for each conversion call.
  Fixture other;
  other.records[3][4] = 'o';
  other.records[3][5] = 'e';
  auto other_rules = other.read();
  ASSERT_TRUE(other_rules.has_value());
  in = umlaut;
  out = buffer;
  EXPECT_EQ(converter.convert(in, out, &*other_rules).value(), size_t(1));
  EXPECT_EQ(buffer[0], uint8_t('o'));
  EXPECT_EQ(buffer[1], uint8_t('e'));
}

TEST(LlvmLibcTransliterationDataTest, DeletionAndFallback) {
  Fixture fixture;
  fixture.records[3][2] = 1;
  fixture.records[3][3] = 0;
  fixture.counts[3] = 4;
  auto rules = fixture.read();
  ASSERT_TRUE(rules.has_value());
  auto converter =
      EncodingConverter::create(TextEncoding::UTF8, TextEncoding::ASCII,
                                EncodingConverter::NonIdentical::TRANSLIT)
          .value();
  const uint8_t input[] = {0xc3, 0xa4, 0xe2, 0x82, 0xac};
  span<const uint8_t> in(input);
  span<uint8_t> out;
  EXPECT_EQ(converter.convert(in, out, &*rules).error(), E2BIG);
  EXPECT_EQ(in.data(), input + 2);
  uint8_t byte = 0;
  out = {&byte, 1};
  EXPECT_EQ(converter.convert(in, out, &*rules).value(), size_t(1));
  EXPECT_EQ(byte, uint8_t('?'));
  EXPECT_TRUE(in.empty());
}

TEST(LlvmLibcTransliterationDataTest,
     MultipleInputCharactersAndIncompletePrefix) {
  Fixture fixture;
  fixture.records[2][1] = 0x100;
  fixture.records[2][5] = 'Z';
  fixture.records[3][1] = 0x200;
  auto rules = fixture.read();
  ASSERT_TRUE(rules.has_value());
  auto converter =
      EncodingConverter::create(TextEncoding::UTF8, TextEncoding::ASCII,
                                EncodingConverter::NonIdentical::TRANSLIT)
          .value();
  const uint8_t input[] = {0xc4, 0x80, 0xcc, 0x81};
  span<const uint8_t> in(input, 2);
  uint8_t byte = 0xcc;
  span<uint8_t> out(&byte, 1);
  EXPECT_EQ(converter.convert(in, out, &*rules).error(), EINVAL);
  EXPECT_EQ(in.data(), input);
  EXPECT_EQ(byte, uint8_t(0xcc));
  in = input;
  EXPECT_EQ(converter.convert(in, out, &*rules).value(), size_t(2));
  EXPECT_TRUE(in.empty());
  EXPECT_EQ(byte, uint8_t('Z'));
  const uint8_t mismatch[] = {0xc4, 0x80, 0};
  uint8_t buffer[2]{};
  in = mismatch;
  out = buffer;
  EXPECT_EQ(converter.convert(in, out, &*rules).value(), size_t(1));
  EXPECT_EQ(buffer[0], uint8_t('?'));
  EXPECT_EQ(buffer[1], uint8_t(0));
  EXPECT_TRUE(in.empty());
}
