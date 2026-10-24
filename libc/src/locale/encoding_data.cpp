//===-- Validated ISO-2022-CN mapping tables
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/encoding_data.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
bool scalar(uint32_t value) {
  return value <= 0x10ffff && (value < 0xd800 || value > 0xdfff);
}
bool position(uint32_t value) {
  return (value >> 8) >= 0x21 && (value >> 8) <= 0x7e &&
         (value & 255) >= 0x21 && (value & 255) <= 0x7e;
}
bool valid(const LocaleDataView::Item &item, bool decode) {
  if (item.kind != LocaleDataView::Kind::WORDS || item.bytes.size() % 8 ||
      item.bytes.size() / 8 > 94 * 94)
    return false;
  uint32_t last = 0;
  for (size_t i = 0; i != item.bytes.size() / 4; i += 2) {
    uint32_t key = item.word(i).value(), value = item.word(i + 1).value();
    if ((i && key <= last) || !(decode ? position(key) : scalar(key)) ||
        !(decode ? scalar(value) : position(value)))
      return false;
    last = key;
  }
  return true;
}
ErrorOr<uint32_t> lookup(const LocaleDataView::Item &item, uint32_t key) {
  size_t begin = 0, end = item.bytes.size() / 8;
  while (begin < end) {
    size_t mid = begin + (end - begin) / 2;
    uint32_t candidate = item.word(mid * 2).value();
    if (key == candidate)
      return item.word(mid * 2 + 1).value();
    if (key < candidate)
      end = mid;
    else
      begin = mid + 1;
  }
  return Error(EILSEQ);
}
} // namespace

ErrorOr<ChineseEncodingData>
ChineseEncodingData::read(const LocaleDataView &file) {
  ChineseEncodingData result;
  for (unsigned set = 0; set != SET_COUNT; ++set) {
    auto decode = file.find(0, 0x201 + 2 * set);
    auto encode = file.find(0, 0x202 + 2 * set);
    if (!decode || !encode || !valid(*decode, true) || !valid(*encode, false))
      return Error(EINVAL);
    for (size_t i = 0; i != encode->bytes.size() / 4; i += 2) {
      uint32_t key = encode->word(i).value();
      auto decoded = lookup(*decode, encode->word(i + 1).value());
      if (!decoded || *decoded != key)
        return Error(EINVAL);
    }
    result.decoders[set] = *decode;
    result.encoders[set] = *encode;
  }
  return result;
}

ErrorOr<uint32_t> ChineseEncodingData::decode(Set set, uint32_t key) const {
  if (set >= SET_COUNT)
    return Error(EINVAL);
  if (!position(key))
    return Error(EILSEQ);
  return lookup(decoders[set], key);
}

ErrorOr<uint32_t> ChineseEncodingData::encode(Set set, uint32_t key) const {
  if (set >= SET_COUNT)
    return Error(EINVAL);
  if (!scalar(key))
    return Error(EILSEQ);
  return lookup(encoders[set], key);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
