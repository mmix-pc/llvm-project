//===-- Prepare BSD hash overflow allocation and release ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hash_overflow.h"
#include "hash_function.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {
namespace {
constexpr uint32_t BITS_PER_MAP = HASH_PAGE_SIZE * 8;

bool valid(const HashHeader &header) {
  unsigned char bytes[HASH_HEADER_SIZE];
  return encode_hash_header(header, hash_signature(), bytes, sizeof(bytes));
}

size_t byte_index(uint32_t bit, bool be) {
  return (bit / 32) * 4 + (be ? 3 - (bit % 32) / 8 : (bit % 32) / 8);
}

bool is_bitmap(const HashHeader &header, uint16_t address) {
  uint32_t total = header.spares[header.overflow_point];
  for (uint32_t i = 0; i < (total + BITS_PER_MAP - 1) / BITS_PER_MAP; ++i)
    if (header.bitmaps[i] == address)
      return true;
  return false;
}

uint16_t logical_address(const HashHeader &header, uint32_t bit) {
  uint32_t split = 0;
  while (bit >= header.spares[split])
    ++split;
  uint32_t previous = split ? header.spares[split - 1] : 0;
  return static_cast<uint16_t>((split << 11) | (bit - previous + 1));
}

// Append a logical page without moving any existing physical page. At the
// 11-bit offset limit start a new split; all shifts remain below bit 31.
ErrorOr<uint16_t> append(HashHeader &header) {
  uint32_t split = header.overflow_point;
  uint32_t previous = split ? header.spares[split - 1] : 0;
  if (header.spares[split] - previous == 0x7ff) {
    if (split == 30)
      return Error(EFBIG);
    previous = header.spares[split];
    header.spares[++split] = previous;
    header.overflow_point = split;
  }
  uint32_t offset = ++header.spares[split] - previous;
  uint16_t address = static_cast<uint16_t>((split << 11) | offset);
  uint32_t physical;
  if (!hash_overflow_page(header, address, physical))
    return Error(EFBIG);
  return address;
}

ErrorOr<size_t> read_bitmap(const HashIO &io, const HashHeader &header,
                            uint32_t map, HashOverflow &plan) {
  if (!hash_overflow_page(header, header.bitmaps[map], plan.bitmap_physical))
    return Error(EINVAL);
  return read_hash_block(io, plan.bitmap, sizeof(plan.bitmap),
                         uint64_t(plan.bitmap_physical) * HASH_PAGE_SIZE);
}
} // namespace

ErrorOr<HashOverflow> plan_hash_overflow(const HashIO &io,
                                         const HashHeader &header) {
  if (!valid(header))
    return Error(EINVAL);
  HashOverflow plan{};
  plan.header = header;
  const uint32_t total = header.spares[header.overflow_point];
  const uint32_t maps = (total + BITS_PER_MAP - 1) / BITS_PER_MAP;
  const bool be = header.byte_order == 4321;
  for (uint32_t map = 0; map < maps; ++map) {
    auto read = read_bitmap(io, header, map, plan);
    if (!read)
      return Error(read.error());
    for (uint32_t local = 0;
         local < BITS_PER_MAP && map * BITS_PER_MAP + local < total; ++local) {
      size_t byte = byte_index(local, be);
      unsigned char mask = static_cast<unsigned char>(1U << (local % 8));
      if (plan.bitmap[byte] & mask)
        continue;
      uint32_t bit = map * BITS_PER_MAP + local;
      plan.address = logical_address(header, bit);
      if (is_bitmap(header, plan.address) ||
          !hash_overflow_page(header, plan.address, plan.physical))
        return Error(EINVAL);
      plan.bitmap[byte] |= mask;
      plan.header.last_freed = bit;
      return plan;
    }
  }

  uint32_t local = total % BITS_PER_MAP;
  if (!local) {
    // The new bitmap consumes a logical page of its own before the data page.
    auto address = append(plan.header);
    if (!address)
      return Error(address.error());
    if (maps >= 32 ||
        !hash_overflow_page(plan.header, *address, plan.bitmap_physical))
      return Error(EFBIG);
    plan.header.bitmaps[maps] = *address;
    for (auto &byte : plan.bitmap)
      byte = 0xff; // Unallocated tail bits stay reserved.
    local = 1;
  }
  auto address = append(plan.header);
  if (!address)
    return Error(address.error());
  plan.address = *address;
  plan.bitmap[byte_index(local, be)] |=
      static_cast<unsigned char>(1U << (local % 8));
  plan.header.last_freed = plan.header.spares[plan.header.overflow_point] - 1;
  if (!valid(plan.header) ||
      !hash_overflow_page(plan.header, plan.address, plan.physical))
    return Error(EFBIG);
  return plan;
}

ErrorOr<HashOverflow> plan_hash_release(const HashIO &io,
                                        const HashHeader &header,
                                        uint16_t address) {
  if (!valid(header) || is_bitmap(header, address))
    return Error(EINVAL);
  HashOverflow plan{};
  plan.header = header;
  plan.address = address;
  if (!hash_overflow_page(header, address, plan.physical))
    return Error(EINVAL);
  uint32_t split = address >> 11;
  uint32_t bit = (split ? header.spares[split - 1] : 0) + (address & 0x7ff) - 1;
  auto read = read_bitmap(io, header, bit / BITS_PER_MAP, plan);
  if (!read)
    return Error(read.error());
  uint32_t local = bit % BITS_PER_MAP;
  size_t byte = byte_index(local, header.byte_order == 4321);
  unsigned char mask = static_cast<unsigned char>(1U << (local % 8));
  if (!(plan.bitmap[byte] & mask))
    return Error(EINVAL);
  plan.bitmap[byte] &= static_cast<unsigned char>(~mask);
  if (bit < header.last_freed)
    plan.header.last_freed = bit;
  return plan;
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
