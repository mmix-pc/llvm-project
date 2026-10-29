//===-- Staged BSD hash writes --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hash_write_batch.h"
#include "hash_database.h"
#include "hash_function.h"
#include "hdr/errno_macros.h"
#include "src/__support/alloc-checker.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

HashWriteBatch::~HashWriteBatch() {
  while (pages) {
    Page *next = pages->next;
    delete pages;
    pages = next;
  }
}

ErrorOr<size_t>
HashWriteBatch::stage(uint32_t physical,
                      const unsigned char (&bytes)[HASH_PAGE_SIZE]) {
  if (started)
    return Error(EIO);
  if (!physical)
    return Error(EINVAL);
  Page *page = pages;
  while (page && page->number != physical)
    page = page->next;
  if (!page) {
    AllocChecker ac;
    page = new (ac) Page;
    if (!ac)
      return Error(ENOMEM);
    page->number = physical;
    page->next = pages;
    pages = page;
  }
  inline_memcpy(page->bytes, bytes, HASH_PAGE_SIZE);
  uint64_t end = (uint64_t(physical) + 1) * HASH_PAGE_SIZE;
  if (end > prepared_size)
    prepared_size = end;
  return HASH_PAGE_SIZE;
}

ErrorOr<size_t> HashWriteBatch::read(void *context, unsigned char *bytes,
                                     size_t count, uint64_t offset) {
  auto &batch = *static_cast<HashWriteBatch *>(context);
  if (batch.started)
    return Error(EIO);
  if (!count || offset >= batch.prepared_size)
    return size_t{0};
  uint64_t available = batch.prepared_size - offset;
  if (count > available)
    count = static_cast<size_t>(available);
  size_t local = offset % HASH_PAGE_SIZE;
  if (count > HASH_PAGE_SIZE - local)
    count = HASH_PAGE_SIZE - local;
  for (Page *page = batch.pages; page; page = page->next) {
    if (page->number == offset / HASH_PAGE_SIZE) {
      inline_memcpy(bytes, page->bytes + local, count);
      return count;
    }
  }
  if (offset < batch.original_size) {
    if (count > batch.original_size - offset)
      count = static_cast<size_t>(batch.original_size - offset);
    if (!batch.io.read_at)
      return Error(EBADF);
    return batch.io.read_at(batch.io.context, bytes, count, offset);
  }
  // Extending positional writes create sparse holes, not readable bytes past
  // the final extent. Existing truncated data still comes from the transport.
  for (size_t i = 0; i < count; ++i)
    bytes[i] = 0;
  return count;
}

ErrorOr<bool> HashWriteBatch::publish(const HashHeader &header,
                                      ErrorOr<size_t> (*synchronize)(void *)) {
  if (started)
    return Error(EIO);
  if (!io.write_at)
    return Error(EBADF);
  unsigned char encoded[HASH_HEADER_SIZE];
  if (!encode_hash_header(header, hash_signature(), encoded, sizeof(encoded)))
    return Error(EINVAL);
  auto valid = validate_hash_database(view(), header, prepared_size);
  if (!valid)
    return Error(valid.error());

  started = true;
  for (Page *page = pages; page; page = page->next) {
    auto write = write_hash_block(io, page->bytes, HASH_PAGE_SIZE,
                                  uint64_t(page->number) * HASH_PAGE_SIZE);
    if (!write)
      return Error(write.error());
  }
  auto write = write_hash_block(io, encoded, sizeof(encoded), 0);
  if (!write)
    return Error(write.error());
  if (synchronize) {
    auto synced = synchronize(io.context);
    if (!synced)
      return Error(synced.error());
  }
  return true;
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
