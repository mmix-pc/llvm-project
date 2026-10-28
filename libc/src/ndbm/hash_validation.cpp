//===-- Validate the private BSD hash database format ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hash_validation.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {
namespace {
uint32_t word(const unsigned char *bytes, size_t index) {
  const unsigned char *p = bytes + 4 * index;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 |
         uint32_t(p[3]);
}
void put_word(unsigned char *bytes, size_t index, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes[4 * index + i] = static_cast<unsigned char>(value >> (24 - 8 * i));
}
uint16_t half(const unsigned char *bytes, size_t index, bool big_endian) {
  const unsigned char *p = bytes + 2 * index;
  return big_endian ? uint16_t(uint16_t(p[0]) << 8 | p[1])
                    : uint16_t(uint16_t(p[1]) << 8 | p[0]);
}
bool power_of_two(uint32_t value) { return value && !(value & (value - 1)); }
bool overflow_address(uint16_t address) {
  // The imported arithmetic uses a signed shift for the split number.
  return (address >> 11) < 31 && (address & 0x7ff) != 0;
}
} // namespace

bool hash_bucket_page(const HashHeader &header, uint32_t bucket,
                      uint32_t &page) {
  if (bucket > header.max_bucket)
    return false;
  uint64_t physical = uint64_t(bucket) + 1;
  if (bucket) {
    uint32_t split = 0;
    for (uint32_t value = bucket; value >>= 1;)
      ++split;
    if (split >= header.overflow_point || split >= 31)
      return false;
    physical += header.spares[split];
  }
  if (physical > 0x7fffffff)
    return false;
  page = uint32_t(physical);
  return true;
}

bool hash_overflow_page(const HashHeader &header, uint16_t address,
                        uint32_t &page) {
  uint32_t split = address >> 11;
  uint32_t offset = address & 0x7ff;
  if (!overflow_address(address) || split > header.overflow_point)
    return false;
  uint32_t previous = split ? header.spares[split - 1] : 0;
  uint32_t end = header.spares[split];
  if (end < previous || offset > end - previous)
    return false;
  uint64_t physical = (uint64_t(1) << split) + previous + offset;
  if (physical > 0x7fffffff)
    return false;
  page = uint32_t(physical);
  return true;
}

bool hash_record_fits(size_t key, size_t content) {
  return key <= HASH_RECORD_LIMIT && content <= HASH_RECORD_LIMIT - key;
}

bool decode_hash_header(const unsigned char *bytes, size_t size,
                        uint32_t expected_hash, HashHeader &out) {
  if (!bytes || size != HASH_HEADER_SIZE)
    return false;
  // The header is always big-endian; the byte-order tag describes pages.
  if (word(bytes, 0) != 0x061561 || word(bytes, 1) != 2 ||
      (word(bytes, 2) != 1234 && word(bytes, 2) != 4321) ||
      word(bytes, 3) != HASH_PAGE_SIZE || word(bytes, 4) != 12 ||
      word(bytes, 6) != 256 || word(bytes, 7) != 8 || word(bytes, 15) != 1 ||
      word(bytes, 16) != expected_hash)
    return false;
  HashHeader h{};
  h.byte_order = word(bytes, 2);
  h.directory_size = word(bytes, 5);
  h.overflow_point = word(bytes, 8);
  h.last_freed = word(bytes, 9);
  h.max_bucket = word(bytes, 10);
  h.high_mask = word(bytes, 11);
  h.low_mask = word(bytes, 12);
  h.fill_factor = word(bytes, 13);
  h.keys = word(bytes, 14);
  if (!power_of_two(h.directory_size) || h.directory_size > 0x40000000 ||
      h.overflow_point >= 31 || !h.fill_factor || h.fill_factor > 0x7fffffff ||
      h.keys > 0x7fffffff || h.high_mask > 0x7fffffff ||
      !power_of_two(h.high_mask + 1) ||
      h.high_mask != uint64_t(h.low_mask) * 2 + 1 ||
      h.max_bucket < h.low_mask || h.max_bucket > h.high_mask ||
      // Keep the imported segment-count rounding representable in int32_t.
      h.max_bucket > 0x7ffffeff ||
      uint64_t(h.max_bucket) / 256 + 1 > h.directory_size)
    return false;
  for (size_t i = 0; i < 32; ++i) {
    h.spares[i] = word(bytes, 17 + i);
    h.bitmaps[i] = half(bytes + 196, i, true);
    if (h.spares[i] > 0x7fffffff ||
        (h.bitmaps[i] && !overflow_address(h.bitmaps[i])))
      return false;
    if (i <= h.overflow_point) {
      uint32_t previous = i ? h.spares[i - 1] : 0;
      if (h.spares[i] < previous || h.spares[i] - previous > 0x7ff)
        return false;
    }
  }
  uint32_t spare_count = h.spares[h.overflow_point];
  uint64_t maps =
      (uint64_t(spare_count) + HASH_PAGE_SIZE * 8 - 1) / (HASH_PAGE_SIZE * 8);
  if (!maps || maps > 32 || h.last_freed > spare_count ||
      uint64_t(h.max_bucket) + 1 + spare_count > 0x7fffffff)
    return false;
  uint32_t physical;
  if (!hash_bucket_page(h, h.max_bucket, physical))
    return false;
  for (size_t i = 0; i < maps; ++i) {
    if (!hash_overflow_page(h, h.bitmaps[i], physical))
      return false;
    for (size_t j = 0; j < i; ++j)
      if (h.bitmaps[i] == h.bitmaps[j])
        return false;
  }
  out = h;
  return true;
}

bool encode_hash_header(const HashHeader &header, uint32_t expected_hash,
                        unsigned char *bytes, size_t size) {
  if (!bytes || size != HASH_HEADER_SIZE)
    return false;
  unsigned char encoded[HASH_HEADER_SIZE];
  const uint32_t fields[] = {0x061561,
                             2,
                             header.byte_order,
                             HASH_PAGE_SIZE,
                             12,
                             header.directory_size,
                             256,
                             8,
                             header.overflow_point,
                             header.last_freed,
                             header.max_bucket,
                             header.high_mask,
                             header.low_mask,
                             header.fill_factor,
                             header.keys,
                             1,
                             expected_hash};
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    put_word(encoded, i, fields[i]);
  for (size_t i = 0; i < 32; ++i) {
    put_word(encoded, 17 + i, header.spares[i]);
    encoded[196 + 2 * i] = static_cast<unsigned char>(header.bitmaps[i] >> 8);
    encoded[197 + 2 * i] = static_cast<unsigned char>(header.bitmaps[i]);
  }
  HashHeader checked{};
  if (!decode_hash_header(encoded, sizeof(encoded), expected_hash, checked))
    return false;
  for (size_t i = 0; i < sizeof(encoded); ++i)
    bytes[i] = encoded[i];
  return true;
}

HashPageStatus validate_hash_page(const unsigned char *bytes, size_t size,
                                  bool big_endian) {
  if (!bytes || size != HASH_PAGE_SIZE)
    return HashPageStatus::Malformed;
  size_t count = half(bytes, 0, big_endian);
  if (count % 2 || count > (HASH_PAGE_SIZE / 2) - 3)
    return HashPageStatus::Malformed;
  if (!count) {
    bool zero_filled = true;
    for (size_t i = 0; i < size; ++i)
      zero_filled &= bytes[i] == 0;
    if (zero_filled)
      return HashPageStatus::ZeroFilled;
  }
  size_t metadata = (count + 3) * 2;
  size_t free = half(bytes, count + 1, big_endian);
  size_t offset = half(bytes, count + 2, big_endian);
  if (offset > size || offset < metadata || free != offset - metadata)
    return HashPageStatus::Malformed;
  size_t previous = size;
  for (size_t i = 1; i <= count; i += 2) {
    size_t key = half(bytes, i, big_endian);
    size_t content = half(bytes, i + 1, big_endian);
    if (!content) {
      if (i != count - 1 || !overflow_address(uint16_t(key)))
        return HashPageStatus::Malformed;
    } else if (content < 4) {
      // Cross-page record opcodes are not admitted by this provider.
      return HashPageStatus::LargeRecord;
    } else {
      if (key > previous || content > key || content < metadata ||
          !hash_record_fits(previous - key, key - content))
        return HashPageStatus::Malformed;
      previous = content;
    }
  }
  return previous == offset ? HashPageStatus::Valid : HashPageStatus::Malformed;
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
