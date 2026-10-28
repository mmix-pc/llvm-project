//===-- Checked hash chain reader -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hash_reader.h"
#include "hash_function.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

bool HashChainReader::page_exists(uint32_t number) const {
  return (uint64_t(number) + 1) * HASH_PAGE_SIZE <= file_size;
}

ErrorOr<bool> HashChainReader::fail(int error) {
  failure = error;
  return Error(error);
}

ErrorOr<size_t> HashChainReader::begin(uint32_t selected) {
  return begin_bucket(selected, true);
}

ErrorOr<size_t> HashChainReader::begin_bucket(uint32_t selected,
                                             bool reset_visited) {
  loaded = false;
  finished = true;
  failure = 0;
  record = 0;
  info = {};
  unsigned char encoded[HASH_HEADER_SIZE];
  if (file_size > 0x7fffffffffffffff ||
      !encode_hash_header(header, hash_signature(), encoded, sizeof(encoded)) ||
      !hash_bucket_page(header, selected, physical)) {
    failure = EINVAL;
    return Error(failure);
  }
  if (!page_exists(physical)) {
    failure = EIO;
    return Error(failure);
  }
  if (reset_visited)
    for (auto &byte : visited)
      byte = 0;
  bucket = selected;
  finished = false;
  return size_t{0};
}

ErrorOr<size_t> HashChainReader::validate_file() {
  auto started = begin(header.max_bucket);
  if (!started)
    return Error(started.error());
  // Fail closed even if validation stops before traversing the first bucket.
  finished = true;
  auto reject = [this](int error) -> ErrorOr<size_t> {
    failure = error;
    return Error(error);
  };
  if (file_size % HASH_PAGE_SIZE)
    return reject(EIO);
  const uint32_t total = header.spares[header.overflow_point];
  const uint32_t maps = (total + HASH_PAGE_SIZE * 8 - 1) / (HASH_PAGE_SIZE * 8);
  for (uint32_t i = 0; i < maps; ++i) {
    uint16_t address = header.bitmaps[i];
    visited[address / 8] |= static_cast<unsigned char>(1U << (address % 8));
  }
  size_t count = 0;
  for (uint32_t selected = 0; selected <= header.max_bucket; ++selected) {
    auto start = begin_bucket(selected, false);
    if (!start)
      return Error(start.error());
    HashRecord entry{};
    for (;;) {
      auto result = next(entry);
      if (!result)
        return Error(result.error());
      if (!*result)
        break;
      if (++count > header.keys)
        return reject(EINVAL);
    }
  }
  if (count != header.keys)
    return reject(EINVAL);

  // Compare every allocated logical page with its unique owner, including the
  // bitmap pages themselves. Unused tail bits are reserved by the BSD format.
  uint32_t bit = 0, split = 0;
  for (uint32_t i = 0; i < maps; ++i) {
    uint32_t bitmap_page;
    if (!hash_overflow_page(header, header.bitmaps[i], bitmap_page))
      return reject(EINVAL);
    if (!page_exists(bitmap_page))
      return reject(EIO);
    auto read = read_hash_block(io, page, sizeof(page),
                               uint64_t(bitmap_page) * HASH_PAGE_SIZE);
    if (!read)
      return reject(read.error());
    for (uint32_t local = 0; local < HASH_PAGE_SIZE * 8 && bit < total;
         ++local, ++bit) {
      while (bit >= header.spares[split])
        ++split;
      uint32_t previous = split ? header.spares[split - 1] : 0;
      uint16_t address =
          static_cast<uint16_t>((split << 11) | (bit - previous + 1));
      uint32_t word = 0;
      for (unsigned byte = 0; byte < 4; ++byte)
        word =
            (word << 8) | page[(local / 32) * 4 +
                               (header.byte_order == 4321 ? byte : 3 - byte)];
      bool allocated = (word & (uint32_t{1} << (local % 32))) != 0;
      bool owned = (visited[address / 8] & (1U << (address % 8))) != 0;
      if (allocated != owned)
        return reject(EINVAL);
    }
  }
  return count;
}

ErrorOr<size_t> HashChainReader::check_overflow(uint16_t address) {
  uint32_t next;
  if (!hash_overflow_page(header, address, next))
    return Error(EINVAL);
  unsigned char mask = static_cast<unsigned char>(1U << (address % 8));
  if (visited[address / 8] & mask)
    return Error(EINVAL);
  uint32_t total = header.spares[header.overflow_point];
  size_t maps =
      (uint64_t(total) + HASH_PAGE_SIZE * 8 - 1) / (HASH_PAGE_SIZE * 8);
  for (size_t i = 0; i < maps; ++i)
    if (address == header.bitmaps[i])
      return Error(EINVAL);
  uint32_t split = address >> 11;
  uint32_t bit = (split ? header.spares[split - 1] : 0) + (address & 0x7ff) - 1;
  uint32_t map_page;
  if (!hash_overflow_page(header, header.bitmaps[bit / (HASH_PAGE_SIZE * 8)],
                          map_page))
    return Error(EINVAL);
  if (!page_exists(next) || !page_exists(map_page))
    return Error(EIO);
  unsigned char word[4];
  uint64_t offset = uint64_t(map_page) * HASH_PAGE_SIZE +
                    ((bit % (HASH_PAGE_SIZE * 8)) / 32) * 4;
  auto read = read_hash_block(io, word, sizeof(word), offset);
  if (!read)
    return Error(read.error());
  uint32_t bits = 0;
  for (unsigned i = 0; i < 4; ++i)
    bits = (bits << 8) | word[header.byte_order == 4321 ? i : 3 - i];
  if (!(bits & (uint32_t{1} << (bit % 32))))
    return Error(EINVAL);
  visited[address / 8] |= mask;
  physical = next;
  return size_t{0};
}

ErrorOr<size_t> HashChainReader::load_page(bool primary) {
  auto status =
      read_hash_data_page(io, physical, header.byte_order == 4321, page);
  if (!status)
    return Error(status.error());
  if (*status == HashPageStatus::ZeroFilled && primary) {
    // A fully present sparse primary page is empty; EOF is never an empty page.
    info = {};
  } else if (!read_hash_page_info(page, sizeof(page), header.byte_order == 4321,
                                  info)) {
    return Error(EINVAL);
  }
  record = 0;
  loaded = true;
  return size_t{0};
}

ErrorOr<bool> HashChainReader::next(HashRecord &out) {
  out = {};
  if (failure)
    return Error(failure);
  if (finished)
    return false;
  if (!loaded) {
    auto result = load_page(true);
    if (!result)
      return fail(result.error());
  }
  for (;;) {
    if (record < info.records) {
      HashRecord entry{};
      if (read_hash_record(page, sizeof(page), header.byte_order == 4321,
                           record, entry) != HashPageResult::Success ||
          hash_key_bucket(header, entry.key, entry.key_size) != bucket)
        return fail(EINVAL);
      ++record;
      out = entry;
      return true;
    }
    if (!info.overflow) {
      finished = true;
      return false;
    }
    auto checked = check_overflow(info.overflow);
    if (!checked)
      return fail(checked.error());
    auto result = load_page(false);
    if (!result)
      return fail(result.error());
  }
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
