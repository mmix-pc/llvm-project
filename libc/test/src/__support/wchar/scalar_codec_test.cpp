//===-- Stateless scalar encoding tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/wchar/scalar_codec.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::decode_scalar;
using LIBC_NAMESPACE::internal::encode_scalar;
using LIBC_NAMESPACE::internal::ScalarEncoding;

namespace {
constexpr ScalarEncoding ENCODINGS[] = {
    ScalarEncoding::ASCII,   ScalarEncoding::LATIN1,  ScalarEncoding::UTF8,
    ScalarEncoding::UTF16LE, ScalarEncoding::UTF16BE, ScalarEncoding::UTF32LE,
    ScalarEncoding::UTF32BE};
struct Vector {
  uint32_t value;
  uint8_t utf8[4];
  unsigned utf8_size;
  uint8_t utf16le[4];
  unsigned utf16_size;
  uint8_t utf32le[4];
};
// Independent code-unit vectors, not only converter round trips.
constexpr Vector VECTORS[] = {
    {0, {0}, 1, {0, 0}, 2, {0, 0, 0, 0}},
    {0x41, {0x41}, 1, {0x41, 0}, 2, {0x41, 0, 0, 0}},
    {0xa3, {0xc2, 0xa3}, 2, {0xa3, 0}, 2, {0xa3, 0, 0, 0}},
    {0x20ac, {0xe2, 0x82, 0xac}, 3, {0xac, 0x20}, 2, {0xac, 0x20, 0, 0}},
    {0xd7ff, {0xed, 0x9f, 0xbf}, 3, {0xff, 0xd7}, 2, {0xff, 0xd7, 0, 0}},
    {0xe000, {0xee, 0x80, 0x80}, 3, {0, 0xe0}, 2, {0, 0xe0, 0, 0}},
    {0xfeff, {0xef, 0xbb, 0xbf}, 3, {0xff, 0xfe}, 2, {0xff, 0xfe, 0, 0}},
    {0x1f600,
     {0xf0, 0x9f, 0x98, 0x80},
     4,
     {0x3d, 0xd8, 0, 0xde},
     4,
     {0, 0xf6, 1, 0}},
    {0x10ffff,
     {0xf4, 0x8f, 0xbf, 0xbf},
     4,
     {0xff, 0xdb, 0xff, 0xdf},
     4,
     {0xff, 0xff, 0x10, 0}},
};

unsigned expected(const Vector &v, ScalarEncoding encoding, uint8_t *bytes) {
  switch (encoding) {
  case ScalarEncoding::ASCII:
    if (v.value > 0x7f)
      return 0;
    [[fallthrough]];
  case ScalarEncoding::LATIN1:
    if (v.value > 0xff)
      return 0;
    bytes[0] = uint8_t(v.value);
    return 1;
  case ScalarEncoding::UTF8:
    for (unsigned i = 0; i != v.utf8_size; ++i)
      bytes[i] = v.utf8[i];
    return v.utf8_size;
  case ScalarEncoding::UTF16LE:
  case ScalarEncoding::UTF16BE:
    for (unsigned i = 0; i != v.utf16_size; ++i)
      bytes[i] = v.utf16le[encoding == ScalarEncoding::UTF16LE ? i : i ^ 1];
    return v.utf16_size;
  case ScalarEncoding::UTF32LE:
  case ScalarEncoding::UTF32BE:
    for (unsigned i = 0; i != 4; ++i)
      bytes[i] = v.utf32le[encoding == ScalarEncoding::UTF32LE ? i : 3 - i];
    return 4;
  }
  return 0;
}
} // namespace

TEST(LlvmLibcScalarCodecTest, IndependentVectorsAndExactProgress) {
  for (ScalarEncoding encoding : ENCODINGS) {
    for (const Vector &vector : VECTORS) {
      uint8_t input[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
      unsigned size = expected(vector, encoding, input + 1);
      if (!size)
        continue;
      auto decoded = decode_scalar(encoding, {input + 1, size + 1});
      ASSERT_TRUE(decoded.has_value());
      EXPECT_EQ(decoded->value, vector.value);
      EXPECT_EQ(decoded->bytes, size_t(size));
      uint8_t output[6] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
      auto encoded = encode_scalar(encoding, vector.value, {output + 1, size});
      ASSERT_TRUE(encoded.has_value());
      EXPECT_EQ(*encoded, size_t(size));
      for (unsigned i = 0; i != size; ++i)
        EXPECT_EQ(output[i + 1], input[i + 1]);
      EXPECT_EQ(output[0], uint8_t(0xaa));
      EXPECT_EQ(output[size + 1], uint8_t(0xaa));
    }
  }
}

TEST(LlvmLibcScalarCodecTest, TruncationAndAtomicOutput) {
  for (ScalarEncoding encoding : ENCODINGS) {
    for (const Vector &vector : VECTORS) {
      uint8_t input[4];
      unsigned size = expected(vector, encoding, input);
      if (!size)
        continue;
      for (unsigned n = 0; n != size; ++n) {
        auto decoded = decode_scalar(encoding, {input, n});
        ASSERT_FALSE(decoded.has_value());
        EXPECT_EQ(decoded.error(), EINVAL);
        uint8_t output[4] = {0xaa, 0xaa, 0xaa, 0xaa};
        auto encoded = encode_scalar(encoding, vector.value, {output, n});
        ASSERT_FALSE(encoded.has_value());
        EXPECT_EQ(encoded.error(), E2BIG);
        for (uint8_t byte : output)
          EXPECT_EQ(byte, uint8_t(0xaa));
      }
    }
  }
}

TEST(LlvmLibcScalarCodecTest, InvalidAndUnrepresentableScalars) {
  const uint32_t invalid[] = {0xd800, 0xdbff,   0xdc00,
                              0xdfff, 0x110000, 0xffffffff};
  for (ScalarEncoding encoding : ENCODINGS) {
    for (uint32_t value : invalid) {
      uint8_t output[4] = {0xaa, 0xaa, 0xaa, 0xaa};
      auto result = encode_scalar(encoding, value, {output, 4});
      ASSERT_FALSE(result.has_value());
      EXPECT_EQ(result.error(), EILSEQ);
      for (uint8_t byte : output)
        EXPECT_EQ(byte, uint8_t(0xaa));
    }
  }
  uint8_t output = 0xaa;
  EXPECT_EQ(encode_scalar(ScalarEncoding::ASCII, 0x80, {&output, 1}).error(),
            EILSEQ);
  EXPECT_EQ(encode_scalar(ScalarEncoding::LATIN1, 0x100, {&output, 1}).error(),
            EILSEQ);
  EXPECT_EQ(output, uint8_t(0xaa));
}

TEST(LlvmLibcScalarCodecTest, InvalidSequences) {
  struct Bad {
    ScalarEncoding encoding;
    uint8_t bytes[4];
    unsigned size;
  };
  const Bad bad[] = {
      {ScalarEncoding::ASCII, {0x80}, 1},
      {ScalarEncoding::UTF8, {0xc0, 0x80}, 2},
      {ScalarEncoding::UTF8, {0xed, 0xa0}, 2},
      {ScalarEncoding::UTF8, {0xf4, 0x90}, 2},
      {ScalarEncoding::UTF8, {0xe2, 0x41}, 2},
      {ScalarEncoding::UTF16LE, {0, 0xdc}, 2},
      {ScalarEncoding::UTF16BE, {0xdc, 0}, 2},
      {ScalarEncoding::UTF16LE, {0, 0xd8, 0x41, 0}, 4},
      {ScalarEncoding::UTF16BE, {0xd8, 0, 0, 0x41}, 4},
      {ScalarEncoding::UTF32LE, {0, 0xd8, 0, 0}, 4},
      {ScalarEncoding::UTF32BE, {0, 0, 0xd8, 0}, 4},
      {ScalarEncoding::UTF32LE, {0, 0, 0x11, 0}, 4},
      {ScalarEncoding::UTF32BE, {0, 0x11, 0, 0}, 4},
  };
  for (const auto &vector : bad) {
    auto result = decode_scalar(vector.encoding, {vector.bytes, vector.size});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EILSEQ);
  }
}

TEST(LlvmLibcScalarCodecTest, AllLatinBytesAndUnknownEncoding) {
  for (unsigned i = 0; i != 256; ++i) {
    uint8_t byte = uint8_t(i);
    auto decoded = decode_scalar(ScalarEncoding::LATIN1, {&byte, 1});
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, i);
    uint8_t output = 0;
    auto encoded = encode_scalar(ScalarEncoding::LATIN1, i, {&output, 1});
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(output, byte);
  }
  uint8_t byte = 0;
  auto unknown = static_cast<ScalarEncoding>(99);
  EXPECT_EQ(decode_scalar(unknown, {&byte, 1}).error(), EINVAL);
  EXPECT_EQ(encode_scalar(unknown, 0, {&byte, 1}).error(), EINVAL);
}
