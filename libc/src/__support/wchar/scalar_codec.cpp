//===-- Stateless scalar encoding primitives
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/wchar/scalar_codec.h"
#include "hdr/errno_macros.h"
#include "src/__support/wchar/character_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
bool valid(uint32_t value) {
  return value <= 0x10ffff && (value < 0xd800 || value > 0xdfff);
}

uint32_t read_unit(const uint8_t *bytes, unsigned size, bool little) {
  uint32_t value = 0;
  for (unsigned i = 0; i != size; ++i)
    value = (value << 8) | bytes[little ? size - 1 - i : i];
  return value;
}

void write_unit(uint8_t *bytes, uint32_t value, unsigned size, bool little) {
  for (unsigned i = 0; i != size; ++i)
    bytes[little ? i : size - 1 - i] = uint8_t(value >> (i * 8));
}
} // namespace

ErrorOr<DecodedScalar> decode_scalar(ScalarEncoding encoding,
                                     cpp::span<const uint8_t> input) {
  if (input.empty())
    return Error(EINVAL);
  switch (encoding) {
  case ScalarEncoding::ASCII:
    if (input[0] > 0x7f)
      return Error(EILSEQ);
    [[fallthrough]];
  case ScalarEncoding::LATIN1:
    return DecodedScalar{input[0], 1};
  case ScalarEncoding::UTF8: {
    mbstate state{};
    CharacterConverter converter(&state, true);
    for (size_t i = 0; i < input.size() && i != 4; ++i) {
      if (int error = converter.push(char8_t(input[i])))
        return Error(error);
      if (converter.isFull())
        return DecodedScalar{uint32_t(converter.pop<char32_t>().value()),
                             i + 1};
    }
    return Error(EINVAL);
  }
  case ScalarEncoding::UTF16LE:
  case ScalarEncoding::UTF16BE: {
    bool little = encoding == ScalarEncoding::UTF16LE;
    if (input.size() < 2)
      return Error(EINVAL);
    uint32_t first = read_unit(input.data(), 2, little);
    if (first >= 0xdc00 && first <= 0xdfff)
      return Error(EILSEQ);
    if (first < 0xd800 || first > 0xdbff)
      return DecodedScalar{first, 2};
    if (input.size() < 4)
      return Error(EINVAL);
    uint32_t second = read_unit(input.data() + 2, 2, little);
    if (second < 0xdc00 || second > 0xdfff)
      return Error(EILSEQ);
    return DecodedScalar{0x10000 + ((first - 0xd800) << 10) + second - 0xdc00,
                         4};
  }
  case ScalarEncoding::UTF32LE:
  case ScalarEncoding::UTF32BE: {
    if (input.size() < 4)
      return Error(EINVAL);
    uint32_t value =
        read_unit(input.data(), 4, encoding == ScalarEncoding::UTF32LE);
    if (!valid(value))
      return Error(EILSEQ);
    return DecodedScalar{value, 4};
  }
  }
  return Error(EINVAL);
}

ErrorOr<size_t> encode_scalar(ScalarEncoding encoding, uint32_t scalar,
                              cpp::span<uint8_t> output) {
  if (!valid(scalar))
    return Error(EILSEQ);
  uint8_t bytes[4];
  size_t size = 0;
  switch (encoding) {
  case ScalarEncoding::ASCII:
    if (scalar > 0x7f)
      return Error(EILSEQ);
    [[fallthrough]];
  case ScalarEncoding::LATIN1:
    if (scalar > 0xff)
      return Error(EILSEQ);
    bytes[0] = uint8_t(scalar);
    size = 1;
    break;
  case ScalarEncoding::UTF8: {
    mbstate state{};
    CharacterConverter converter(&state, true);
    if (int error = converter.push(char32_t(scalar)))
      return Error(error);
    size = converter.sizeAs<char8_t>();
    for (size_t i = 0; i != size; ++i)
      bytes[i] = uint8_t(converter.pop<char8_t>().value());
    break;
  }
  case ScalarEncoding::UTF16LE:
  case ScalarEncoding::UTF16BE: {
    bool little = encoding == ScalarEncoding::UTF16LE;
    if (scalar <= 0xffff) {
      size = 2;
      write_unit(bytes, scalar, 2, little);
    } else {
      size = 4;
      write_unit(bytes, 0xd800 + ((scalar - 0x10000) >> 10), 2, little);
      write_unit(bytes + 2, 0xdc00 + ((scalar - 0x10000) & 0x3ff), 2, little);
    }
    break;
  }
  case ScalarEncoding::UTF32LE:
  case ScalarEncoding::UTF32BE:
    size = 4;
    write_unit(bytes, scalar, 4, encoding == ScalarEncoding::UTF32LE);
    break;
  }
  if (size == 0)
    return Error(EINVAL);
  if (output.size() < size)
    return Error(E2BIG);
  for (size_t i = 0; i != size; ++i)
    output[i] = bytes[i];
  return size;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
