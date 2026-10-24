//===-- ISO-2022-CN state machine tests
//------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/wchar/iso2022_cn.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::ChineseEncodingData;
using LIBC_NAMESPACE::internal::ISO2022CNState;
using LIBC_NAMESPACE::internal::LocaleDataView;
using Event = ISO2022CNState::Event;

namespace {
// Small deliberately constructed repertoires make wire choices independent of
// production-table round trips. Data-source qualification is separate.
struct Fixture {
  uint8_t bytes[512]{};
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
    put(bytes + 16, 322);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    const uint32_t values[][4] = {
        {0x2121, 0x4e2d, 0x2122, 0x6587},   {0x4e2d, 0x2121, 0x6587, 0x2122},
        {0x2221, 0x4e2d, 0x2222, 0x56fd},   {0x4e2d, 0x2221, 0x56fd, 0x2222},
        {0x2321, 0x20000, 0x2322, 0x20001}, {0x20000, 0x2321, 0x20001, 0x2322}};
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *directory = bytes + 32 + i * 16;
      put(directory, i);
      put(directory + 4, i ? 322 : 130);
      put(directory + 8, i ? 0 : 192);
      put(directory + 12, i ? 0 : 6);
      uint8_t *entry = bytes + 130 + i * 16;
      put(entry, 0x201 + i);
      put(entry + 4, 2);
      put(entry + 8, 96 + i * 16);
      put(entry + 12, 16);
      for (unsigned j = 0; j != 4; ++j)
        put(bytes + 226 + i * 16 + j * 4, values[i][j]);
    }
  }
  auto read() {
    auto file = LocaleDataView::parse({bytes, 322});
    return ChineseEncodingData::read(file.value());
  }
};

void encoded(ISO2022CNState &state, const ChineseEncodingData &data,
             uint32_t scalar, const char *expected, size_t count) {
  for (size_t capacity = 0; capacity < count; ++capacity) {
    ISO2022CNState copy = state;
    uint8_t bytes[10];
    for (auto &byte : bytes)
      byte = 0xcc;
    auto short_output = copy.encode(scalar, {bytes + 1, capacity}, data);
    ASSERT_FALSE(short_output.has_value());
    EXPECT_EQ(short_output.error(), E2BIG);
    for (auto byte : bytes)
      EXPECT_EQ(byte, uint8_t(0xcc));
    auto retry = copy.encode(scalar, {bytes + 1, count}, data);
    ASSERT_TRUE(retry.has_value());
    EXPECT_EQ(*retry, count);
    for (size_t i = 0; i != count; ++i)
      EXPECT_EQ(bytes[i + 1], uint8_t(expected[i]));
    EXPECT_EQ(bytes[0], uint8_t(0xcc));
    EXPECT_EQ(bytes[count + 1], uint8_t(0xcc));
  }
  uint8_t output[8];
  auto result = state.encode(scalar, output, data);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, count);
  for (size_t i = 0; i != count; ++i)
    EXPECT_EQ(output[i], uint8_t(expected[i]));
}
} // namespace

TEST(LlvmLibcISO2022CNTest, EncodingChoicesAndAtomicCapacity) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  EXPECT_TRUE(state.initial());
  encoded(state, *data, 0x4e2d, "\x1b$)A\x0e!!", 7);
  encoded(state, *data, 0x56fd, "\x1b$)G\"\"", 6);
  encoded(state, *data, 0x4e2d, "\"!", 2);
  encoded(state, *data, 'A',
          "\x0f"
          "A",
          2);
  EXPECT_FALSE(state.initial());
  encoded(state, *data, 0x4e2d, "\x0e\"!", 3);
  encoded(state, *data, 0x20000, "\x1b$*H\x1bN#!", 8);
  encoded(state, *data, 0x4e2d, "\"!", 2);
  encoded(state, *data, '\n', "\x0f\n", 2);
  EXPECT_TRUE(state.initial());
  encoded(state, *data, 0x4e2d, "\x1b$)A\x0e!!", 7);
  encoded(state, *data, 0, "\x0f\0", 2);
  EXPECT_TRUE(state.initial());
}

TEST(LlvmLibcISO2022CNTest, IncrementalDecodeAndCopies) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  const char wire[] = "\x1b$)A\x0e!!\x1b$*H\x1bN#!\"!";
  // Copy at every byte boundary, including designation and SS2 prefixes.
  for (size_t split = 0; split < sizeof(wire); ++split) {
    ISO2022CNState state;
    unsigned scalars = 0;
    const uint32_t expected[] = {0x4e2d, 0x20000};
    for (size_t i = 0; i + 3 < sizeof(wire); ++i) {
      if (i == split) {
        ISO2022CNState copy = state;
        state = copy;
      }
      auto step = state.push(uint8_t(wire[i]), *data);
      ASSERT_TRUE(step.has_value());
      if (step->event == Event::SCALAR) {
        ASSERT_LT(scalars, 2U);
        EXPECT_EQ(step->scalar, expected[scalars++]);
      }
    }
    EXPECT_EQ(scalars, 2U);
    EXPECT_TRUE(state.boundary());
    EXPECT_FALSE(state.initial());
    // SS2 did not change the GB locking shift.
    ASSERT_TRUE(state.push('!', *data).has_value());
    auto last = state.push('"', *data);
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->scalar, uint32_t(0x6587));
  }
}

TEST(LlvmLibcISO2022CNTest, StrictDesignationAndInvalidInput) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  EXPECT_EQ(state.push(0x0e, *data).error(), EILSEQ);
  EXPECT_TRUE(state.initial());
  ASSERT_TRUE(state.push(0x1b, *data).has_value());
  EXPECT_EQ(state.push('N', *data).error(), EILSEQ);
  EXPECT_EQ(state.push('O', *data).error(), EILSEQ);
  EXPECT_EQ(state.push(0, *data).error(), EILSEQ);
  EXPECT_FALSE(state.boundary());
  ASSERT_TRUE(state.push('$', *data).has_value());
  ASSERT_TRUE(state.push(')', *data).has_value());
  ASSERT_TRUE(state.push('A', *data).has_value());
  ASSERT_TRUE(state.push(0x0e, *data).has_value());
  EXPECT_EQ(state.push('\n', *data).error(), EILSEQ);
  ASSERT_TRUE(state.push('!', *data).has_value());
  EXPECT_EQ(state.push(0, *data).error(), EILSEQ);
  EXPECT_EQ(state.push(0x80, *data).error(), EILSEQ);
  EXPECT_EQ(state.push('~', *data).error(), EILSEQ);
  auto retry = state.push('!', *data);
  ASSERT_TRUE(retry.has_value());
  EXPECT_EQ(retry->scalar, uint32_t(0x4e2d));
}

TEST(LlvmLibcISO2022CNTest, ResetAndLineBoundaries) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  encoded(state, *data, 0x4e2d, "\x1b$)A\x0e!!", 7);
  uint8_t output = 0xcc;
  EXPECT_EQ(state.reset({&output, size_t(0)}).error(), E2BIG);
  EXPECT_EQ(output, uint8_t(0xcc));
  EXPECT_EQ(state.reset({&output, 1}).value(), size_t(1));
  EXPECT_EQ(output, uint8_t(0x0f));
  EXPECT_TRUE(state.initial());
  EXPECT_EQ(state.reset({}).value(), size_t(0));
  encoded(state, *data, 0x4e2d, "\x1b$)A\x0e!!", 7);
  ASSERT_TRUE(state.push(0x0f, *data).has_value());
  EXPECT_FALSE(state.initial());
  auto line = state.push('\n', *data);
  ASSERT_TRUE(line.has_value());
  EXPECT_EQ(line->scalar, uint32_t('\n'));
  EXPECT_TRUE(state.initial());
  ASSERT_TRUE(state.push(0x1b, *data).has_value());
  EXPECT_EQ(state.reset({&output, 1}).error(), EINVAL);
}

TEST(LlvmLibcISO2022CNTest, EncodingErrorsPreserveOutputAndState) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  const uint32_t invalid[] = {0x0e, 0x0f, 0x1b, 0xd800, 0x110000, 0x1234};
  for (uint32_t scalar : invalid) {
    uint8_t bytes[8]{};
    auto error = state.encode(scalar, bytes, *data);
    ASSERT_FALSE(error.has_value());
    EXPECT_EQ(error.error(), EILSEQ);
    for (uint8_t byte : bytes)
      EXPECT_EQ(byte, uint8_t(0));
    EXPECT_TRUE(state.initial());
  }
  ASSERT_TRUE(state.push(0x1b, *data).has_value());
  uint8_t bytes[8]{};
  EXPECT_EQ(state.encode('A', bytes, *data).error(), EINVAL);
}
