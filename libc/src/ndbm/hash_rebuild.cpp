//===-- Rebuild a staged BSD hash bucket
//-----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hash_rebuild.h"
#include "hash_function.h"
#include "hash_overflow.h"
#include "hash_reader.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

ErrorOr<HashHeader>
rebuild_hash_bucket(HashWriteBatch &batch, const HashHeader &existing,
                    const HashHeader &layout, uint32_t bucket,
                    const HashRecord *records, size_t count) {
  if (count > UINT32_MAX)
    return Error(EOVERFLOW);
  if (count && !records)
    return Error(EINVAL);
  unsigned char encoded[HASH_HEADER_SIZE];
  if (!encode_hash_header(layout, hash_signature(), encoded, sizeof(encoded)) ||
      existing.byte_order != layout.byte_order)
    return Error(EINVAL);
  uint32_t primary;
  if (!hash_bucket_page(layout, bucket, primary))
    return Error(EINVAL);
  for (size_t i = 0; i < count; ++i) {
    if (!hash_record_fits(records[i].key_size, records[i].content_size))
      return Error(EINVAL);
    if ((records[i].key_size && !records[i].key) ||
        (records[i].content_size && !records[i].content) ||
        hash_key_bucket(layout, records[i].key, records[i].key_size) != bucket)
      return Error(EINVAL);
  }

  size_t old_count = 0;
  uint16_t overflow = 0;
  bool be = layout.byte_order == 4321;
  unsigned char page[HASH_PAGE_SIZE];
  if (bucket <= existing.max_bucket) {
    uint32_t original;
    if (!hash_bucket_page(existing, bucket, original) || original != primary)
      return Error(EINVAL);
    // Reject malformed/cyclic/wrong-bucket input before releasing allocation.
    HashChainReader reader(batch.view(), existing, batch.size());
    auto begin = reader.begin(bucket);
    if (!begin)
      return Error(begin.error());
    HashRecord record{};
    for (;;) {
      auto next = reader.next(record);
      if (!next)
        return Error(next.error());
      if (!*next)
        break;
      if (++old_count > existing.keys)
        return Error(EINVAL);
    }
    auto read = read_hash_data_page(batch.view(), primary, be, page);
    if (!read)
      return Error(read.error());
    if (*read == HashPageStatus::Valid) {
      HashPageInfo info;
      if (!read_hash_page_info(page, sizeof(page), be, info))
        return Error(EINVAL);
      overflow = info.overflow;
    }
  }
  if (old_count > layout.keys || count > UINT32_MAX - (layout.keys - old_count))
    return Error(EOVERFLOW);

  HashHeader prepared = layout;
  while (overflow) {
    uint32_t physical;
    if (!hash_overflow_page(prepared, overflow, physical))
      return Error(EINVAL);
    auto read = read_hash_data_page(batch.view(), physical, be, page);
    if (!read)
      return Error(read.error());
    HashPageInfo info;
    if (*read != HashPageStatus::Valid ||
        !read_hash_page_info(page, sizeof(page), be, info))
      return Error(EINVAL);
    auto released = plan_hash_release(batch.view(), prepared, overflow);
    if (!released)
      return Error(released.error());
    auto staged = batch.stage(released->bitmap_physical, released->bitmap);
    if (!staged)
      return Error(staged.error());
    prepared = released->header;
    overflow = info.overflow;
  }

  initialize_hash_page(page, sizeof(page), be);
  uint32_t physical = primary;
  for (size_t i = 0; i < count; ++i) {
    auto appended = append_hash_record(page, sizeof(page), be, records[i]);
    if (appended == HashPageResult::Full) {
      auto allocation = plan_hash_overflow(batch.view(), prepared);
      if (!allocation)
        return Error(allocation.error());
      if (set_hash_overflow(page, sizeof(page), be, allocation->address) !=
          HashPageResult::Success)
        return Error(EINVAL);
      auto staged = batch.stage(physical, page);
      if (!staged)
        return Error(staged.error());
      staged = batch.stage(allocation->bitmap_physical, allocation->bitmap);
      if (!staged)
        return Error(staged.error());
      prepared = allocation->header;
      physical = allocation->physical;
      initialize_hash_page(page, sizeof(page), be);
      appended = append_hash_record(page, sizeof(page), be, records[i]);
    }
    if (appended != HashPageResult::Success)
      return Error(EINVAL);
  }
  auto staged = batch.stage(physical, page);
  if (!staged)
    return Error(staged.error());
  prepared.keys = static_cast<uint32_t>(layout.keys - old_count + count);
  return prepared;
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
