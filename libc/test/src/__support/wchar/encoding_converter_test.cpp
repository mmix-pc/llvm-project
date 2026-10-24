//===-- Transactional conversion tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/wchar/encoding_converter.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::cpp::span;
using LIBC_NAMESPACE::internal::ChineseEncodingData;
using LIBC_NAMESPACE::internal::EncodingConverter;
using LIBC_NAMESPACE::internal::LocaleDataView;
using LIBC_NAMESPACE::internal::TextEncoding;
using Policy = EncodingConverter::NonIdentical;

namespace {
struct Mapping {
  uint8_t bytes[274]{};
  static void word(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - 8 * i));
  }
  Mapping() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    word(bytes + 8, 1);
    word(bytes + 12, 0x01020304);
    word(bytes + 16, sizeof(bytes));
    word(bytes + 20, 6);
    word(bytes + 24, 128);
    word(bytes + 28, 2);
    bytes[128] = 'x';
    const uint32_t scalars[] = {0x4e2d, 0x56fd, 0x20000};
    for (unsigned i = 0; i != 6; ++i) {
      auto *directory = bytes + 32 + 16 * i;
      word(directory, i);
      word(directory + 4, i ? sizeof(bytes) : 130);
      word(directory + 8, i ? 0 : 144);
      word(directory + 12, i ? 0 : 6);
      auto *entry = bytes + 130 + 16 * i;
      word(entry, 0x201 + i);
      word(entry + 4, 2);
      word(entry + 8, 96 + 8 * i);
      word(entry + 12, 8);
      word(bytes + 226 + 8 * i, i % 2 ? scalars[i / 2] : 0x2121);
      word(bytes + 230 + 8 * i, i % 2 ? 0x2121 : scalars[i / 2]);
    }
  }
  auto read() {
    return ChineseEncodingData::read(LocaleDataView::parse(bytes).value());
  }
};
} // namespace

TEST(LlvmLibcEncodingConverterTest, PairValidationAndEmptyInput) {
  auto unknown = static_cast<TextEncoding>(99);
  EXPECT_EQ(EncodingConverter::create(unknown, TextEncoding::ASCII).error(),
            EINVAL);
  EXPECT_EQ(EncodingConverter::create(TextEncoding::ASCII, unknown).error(),
            EINVAL);
  EXPECT_EQ(
      EncodingConverter::create(TextEncoding::ISO2022CN, TextEncoding::ASCII)
          .error(),
      EINVAL);
  auto converter =
      EncodingConverter::create(TextEncoding::UTF8, TextEncoding::UTF16BE);
  ASSERT_TRUE(converter.has_value());
  span<const uint8_t> in;
  span<uint8_t> out;
  EXPECT_EQ(converter->convert(in, out).value(), size_t(0));
  EXPECT_EQ(converter->reset(&out).value(), size_t(0));
}

TEST(LlvmLibcEncodingConverterTest, ExplicitNamesAndDestinationIndicators) {
  Mapping fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  const char *names[] = {"ASCII",    "us-ascii", "ISO-8859-1",
                         "utf-8",    "UTF-16LE", "UTF-16BE",
                         "UTF-32LE", "UTF-32BE", "ISO-2022-CN"};
  for (const char *from : names)
    for (const char *to : names)
      EXPECT_TRUE(
          EncodingConverter::from_names(from, to, &*mapping).has_value());
  const char *invalid[] = {"",
                           " UTF-8",
                           "UTF-8 ",
                           "UTF-16",
                           "UTF-32",
                           "UTF-7",
                           "UTF-8/IGNORE",
                           "UTF-8//",
                           "UTF-8//unknown",
                           "UTF-8//IGNORE//TRANSLIT",
                           "UTF-8//IGNORE/"};
  for (const char *name : invalid) {
    EXPECT_EQ(EncodingConverter::from_names(name, "ASCII").error(), EINVAL);
    EXPECT_EQ(EncodingConverter::from_names("ASCII", name).error(), EINVAL);
  }
  const char *suffixes[] = {"ASCII//NON_IDENTICAL_DISCARD", "ascii//ignore",
                            "ASCII//TRANSLIT"};
  for (const char *name : suffixes) {
    EXPECT_TRUE(EncodingConverter::from_names("UTF-8", name).has_value());
    EXPECT_EQ(EncodingConverter::from_names(name, "UTF-8").error(), EINVAL);
  }
  const char embedded_nul[] = "UTF-8\0extra";
  EXPECT_EQ(EncodingConverter::from_names(
                {embedded_nul, sizeof(embedded_nul) - 1}, "ASCII")
                .error(),
            EINVAL);
  auto converter =
      EncodingConverter::from_names("UTF-8", "ASCII//NON_IDENTICAL_DISCARD");
  ASSERT_TRUE(converter.has_value());
  const uint8_t input[] = {0xc3, 0xa4};
  span<const uint8_t> in(input);
  span<uint8_t> out;
  EXPECT_EQ(converter->convert(in, out).value(), size_t(1));
  EXPECT_TRUE(in.empty());
}

TEST(LlvmLibcEncodingConverterTest, PartialOutputAndRetry) {
  const uint8_t bytes[] = {'A', 0xe2, 0x82, 0xac, 0, 0xef, 0xbb, 0xbf};
  const uint8_t expected[] = {0, 'A', 0x20, 0xac, 0, 0, 0xfe, 0xff};
  for (size_t capacity = 0; capacity != 8; ++capacity) {
    auto converter =
        EncodingConverter::create(TextEncoding::UTF8, TextEncoding::UTF16BE)
            .value();
    uint8_t buffer[9];
    for (auto &byte : buffer)
      byte = 0xcc;
    span<const uint8_t> in(bytes);
    span<uint8_t> out(buffer, capacity);
    EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
    size_t completed = capacity / 2;
    const size_t consumed[] = {0, 1, 4, 5};
    EXPECT_EQ(in.data(), bytes + consumed[completed]);
    EXPECT_EQ(in.size(), sizeof(bytes) - consumed[completed]);
    EXPECT_EQ(out.data(), buffer + completed * 2);
    EXPECT_EQ(out.size(), capacity % 2);
    for (size_t i = 0; i != completed * 2; ++i)
      EXPECT_EQ(buffer[i], expected[i]);
    for (size_t i = completed * 2; i != sizeof(buffer); ++i)
      EXPECT_EQ(buffer[i], uint8_t(0xcc));
    out = {buffer + completed * 2, 8 - completed * 2};
    EXPECT_EQ(converter.convert(in, out).value(), size_t(0));
    EXPECT_TRUE(in.empty());
    EXPECT_TRUE(out.empty());
    for (size_t i = 0; i != 8; ++i)
      EXPECT_EQ(buffer[i], expected[i]);
  }
}

TEST(LlvmLibcEncodingConverterTest, InvalidAndIncompleteRemainUnconsumed) {
  const uint8_t invalid[] = {'A', 0xe2, 0x41};
  const uint8_t incomplete[] = {'A', 0xe2, 0x82};
  const span<const uint8_t> cases[] = {invalid, incomplete};
  for (unsigned i = 0; i != 2; ++i) {
    auto converter =
        EncodingConverter::create(TextEncoding::UTF8, TextEncoding::ASCII)
            .value();
    uint8_t buffer[8]{};
    auto in = cases[i];
    span<uint8_t> out(buffer);
    EXPECT_EQ(converter.convert(in, out).error(), i ? EINVAL : EILSEQ);
    EXPECT_EQ(in.data(), cases[i].data() + 1);
    EXPECT_EQ(in.size(), size_t(2));
    EXPECT_EQ(out.data(), buffer + 1);
    EXPECT_EQ(buffer[0], uint8_t('A'));
    EXPECT_EQ(buffer[1], uint8_t(0));
  }
}

TEST(LlvmLibcEncodingConverterTest, ReplacementDiscardAndCallCounts) {
  const uint8_t bytes[] = {0xe2, 0x82, 0xac, 'A', 0xc2, 0xa3};
  auto converter =
      EncodingConverter::create(TextEncoding::UTF8, TextEncoding::ASCII)
          .value();
  span<const uint8_t> in(bytes);
  span<uint8_t> out;
  EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
  EXPECT_EQ(in.data(), bytes);
  uint8_t buffer[3]{};
  out = {buffer, 1};
  EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
  EXPECT_EQ(buffer[0], uint8_t('?'));
  EXPECT_EQ(in.data(), bytes + 3);
  out = {buffer + 1, 2};
  EXPECT_EQ(converter.convert(in, out).value(), size_t(1));
  EXPECT_EQ(buffer[1], uint8_t('A'));
  EXPECT_EQ(buffer[2], uint8_t('?'));

  converter = EncodingConverter::create(TextEncoding::UTF8, TextEncoding::ASCII,
                                        Policy::DISCARD)
                  .value();
  in = bytes;
  out = {};
  EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
  EXPECT_EQ(in.data(), bytes + 3);
  out = {buffer, 1};
  EXPECT_EQ(converter.convert(in, out).value(), size_t(1));
  EXPECT_TRUE(in.empty());
  EXPECT_EQ(buffer[0], uint8_t('A'));
  in = {bytes, 3};
  out = {};
  EXPECT_EQ(converter.convert(in, out).value(), size_t(1));
  EXPECT_TRUE(in.empty());
  EXPECT_EQ(out.data(), static_cast<uint8_t *>(nullptr));
  const uint8_t bad[] = {0xff};
  in = bad;
  EXPECT_EQ(converter.convert(in, out).error(), EILSEQ);
  EXPECT_EQ(in.data(), bad);
}

TEST(LlvmLibcEncodingConverterTest, ShiftProgressAndSplitRollback) {
  Mapping fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  const uint8_t wire[] = {0x1b, '$', ')', 'A', 0x0e, '!', '!'};
  for (size_t size = 1; size != sizeof(wire); ++size) {
    auto converter =
        EncodingConverter::create(TextEncoding::ISO2022CN, TextEncoding::UTF8,
                                  Policy::REPLACE, &*mapping)
            .value();
    span<const uint8_t> in(wire, size);
    uint8_t buffer[3]{};
    span<uint8_t> out(buffer);
    auto result = converter.convert(in, out);
    if (size == 4 || size == 5) {
      EXPECT_EQ(result.value(), size_t(0));
    } else {
      EXPECT_EQ(result.error(), EINVAL);
    }
    size_t consumed = size < 4 ? 0 : size < 5 ? 4 : 5;
    EXPECT_EQ(in.data(), wire + consumed);
    EXPECT_EQ(out.data(), buffer);
    in = {wire + consumed, sizeof(wire) - consumed};
    EXPECT_EQ(converter.convert(in, out).value(), size_t(0));
    EXPECT_TRUE(in.empty());
    EXPECT_EQ(buffer[0], uint8_t(0xe4));
    EXPECT_EQ(buffer[1], uint8_t(0xb8));
    EXPECT_EQ(buffer[2], uint8_t(0xad));
  }
  auto converter =
      EncodingConverter::create(TextEncoding::ISO2022CN, TextEncoding::UTF8,
                                Policy::REPLACE, &*mapping)
          .value();
  span<const uint8_t> in(wire);
  span<uint8_t> out;
  EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
  EXPECT_EQ(in.data(), wire + 5);
  uint8_t buffer[3];
  out = buffer;
  EXPECT_EQ(converter.convert(in, out).value(), size_t(0));
}

TEST(LlvmLibcEncodingConverterTest, StatefulOutputResetAndIndependentState) {
  Mapping fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  const uint8_t chinese[] = {0xe4, 0xb8, 0xad};
  const uint8_t expected[] = {0x1b, '$', ')', 'A', 0x0e, '!', '!'};
  for (size_t capacity = 0; capacity != 7; ++capacity) {
    auto converter =
        EncodingConverter::create(TextEncoding::UTF8, TextEncoding::ISO2022CN,
                                  Policy::REPLACE, &*mapping)
            .value();
    auto other = converter;
    uint8_t buffer[8];
    for (auto &byte : buffer)
      byte = 0xcc;
    span<const uint8_t> in(chinese);
    span<uint8_t> out(buffer, capacity);
    EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
    EXPECT_EQ(in.data(), chinese);
    EXPECT_EQ(out.data(), buffer);
    for (auto byte : buffer)
      EXPECT_EQ(byte, uint8_t(0xcc));
    out = buffer;
    EXPECT_EQ(converter.convert(in, out).value(), size_t(0));
    for (size_t i = 0; i != 7; ++i)
      EXPECT_EQ(buffer[i], expected[i]);
    span<uint8_t> empty;
    EXPECT_EQ(converter.reset(&empty).error(), E2BIG);
    EXPECT_EQ(other.reset(&empty).value(), size_t(0));
    // Failed reset retained the locking shift: the next character needs no
    // designation or SO, and a subsequent successful reset emits SI.
    in = chinese;
    out = {buffer, 2};
    EXPECT_EQ(converter.convert(in, out).value(), size_t(0));
    EXPECT_EQ(buffer[0], uint8_t('!'));
    EXPECT_EQ(buffer[1], uint8_t('!'));
    out = {buffer, 1};
    EXPECT_EQ(converter.reset(&out).value(), size_t(0));
    EXPECT_EQ(buffer[0], uint8_t(0x0f));
    EXPECT_TRUE(out.empty());
    in = chinese;
    out = buffer;
    EXPECT_EQ(converter.convert(in, out).value(), size_t(0));
    EXPECT_EQ(buffer[0], uint8_t(0x1b));
    EXPECT_EQ(converter.reset(nullptr).value(), size_t(0));
    EXPECT_EQ(converter.reset(&empty).value(), size_t(0));
  }
}

TEST(LlvmLibcEncodingConverterTest, ReplacementUsesDestinationShiftRules) {
  Mapping fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  auto converter =
      EncodingConverter::create(TextEncoding::UTF8, TextEncoding::ISO2022CN,
                                Policy::REPLACE, &*mapping)
          .value();
  const uint8_t input[] = {0xe4, 0xb8, 0xad, 0xe2, 0x82, 0xac};
  uint8_t buffer[9]{};
  span<const uint8_t> in(input);
  span<uint8_t> out(buffer, 8);
  EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
  EXPECT_EQ(in.data(), input + 3);
  EXPECT_EQ(buffer[7], uint8_t(0));
  out = {buffer + 7, 2};
  EXPECT_EQ(converter.convert(in, out).value(), size_t(1));
  EXPECT_EQ(buffer[7], uint8_t(0x0f));
  EXPECT_EQ(buffer[8], uint8_t('?'));
}

TEST(LlvmLibcEncodingConverterTest, IgnoreRequiresValidSuccessor) {
  auto converter = EncodingConverter::create(
                       TextEncoding::UTF8, TextEncoding::ASCII, Policy::IGNORE)
                       .value();
  const uint8_t bytes[] = {0xff, 0xfe, 'A', 0xe2, 0x82, 0xac, 0xc2};
  uint8_t buffer[3]{};
  span<const uint8_t> in(bytes);
  span<uint8_t> out;
  EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
  EXPECT_EQ(in.data(), bytes + 2);
  out = buffer;
  EXPECT_EQ(converter.convert(in, out).error(), EINVAL);
  EXPECT_EQ(buffer[0], uint8_t('A'));
  EXPECT_EQ(in.data(), bytes + 6);
  EXPECT_EQ(out.data(), buffer + 1);
  const uint8_t bad_tail[] = {0xff, 0xc2};
  in = bad_tail;
  EXPECT_EQ(converter.convert(in, out).error(), EILSEQ);
  EXPECT_EQ(in.data(), bad_tail);
  const uint8_t complete[] = {0xff, 'A', 0xe2, 0x82, 0xac};
  in = complete;
  out = buffer;
  EXPECT_EQ(converter.convert(in, out).value(), size_t(1));
  EXPECT_TRUE(in.empty());
  EXPECT_EQ(out.data(), buffer + 1);
}

TEST(LlvmLibcEncodingConverterTest, IgnorePreservesSourceUnitAlignment) {
  struct Case {
    TextEncoding encoding;
    uint8_t bytes[8];
    size_t size, skipped;
  };
  const Case cases[] = {
      {TextEncoding::UTF16BE, {0xdc, 0, 0, 'A'}, 4, 2},
      {TextEncoding::UTF16LE, {0, 0xdc, 'A', 0}, 4, 2},
      {TextEncoding::UTF32BE, {0, 0x11, 0, 'X', 0, 0, 0, 'A'}, 8, 4},
      {TextEncoding::UTF32LE, {'X', 0, 0x11, 0, 'A', 0, 0, 0}, 8, 4}};
  for (const auto &test : cases) {
    auto converter = EncodingConverter::create(
                         test.encoding, TextEncoding::ASCII, Policy::IGNORE)
                         .value();
    span<const uint8_t> in(test.bytes, test.size);
    span<uint8_t> out;
    EXPECT_EQ(converter.convert(in, out).error(), E2BIG);
    EXPECT_EQ(in.data(), test.bytes + test.skipped);
    uint8_t byte = 0;
    out = {&byte, 1};
    EXPECT_EQ(converter.convert(in, out).value(), size_t(0));
    EXPECT_TRUE(in.empty());
    EXPECT_EQ(byte, uint8_t('A'));
  }
}

TEST(LlvmLibcEncodingConverterTest, IgnoreDoesNotApplyInvalidShiftState) {
  Mapping fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  auto converter =
      EncodingConverter::create(TextEncoding::ISO2022CN, TextEncoding::ASCII,
                                Policy::IGNORE, &*mapping)
          .value();
  // Undesignated SO is rejected, then discarded before a valid ASCII byte.
  const uint8_t bytes[] = {0x0e, 'A'};
  span<const uint8_t> in(bytes);
  uint8_t byte = 0;
  span<uint8_t> out(&byte, 1);
  EXPECT_EQ(converter.convert(in, out).value(), size_t(0));
  EXPECT_EQ(byte, uint8_t('A'));
  const uint8_t incomplete[] = {0x1b, '$'};
  in = incomplete;
  EXPECT_EQ(converter.convert(in, out).error(), EINVAL);
  EXPECT_EQ(in.data(), incomplete);
}
