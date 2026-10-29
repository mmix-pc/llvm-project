//===-- Private file-backed DBM engine ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "database.h"
#include "hdr/errno_macros.h"
#include "src/__support/alloc-checker.h"
#include "src/ndbm/hash_function.h"
#include "src/ndbm/hash_growth.h"
#include "src/ndbm/hash_rebuild.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {
namespace {

bool equal_key(const HashRecord &a, const void *key, size_t size) {
  if (a.key_size != size)
    return false;
  auto *bytes = static_cast<const unsigned char *>(key);
  for (size_t i = 0; i < size; ++i)
    if (a.key[i] != bytes[i])
      return false;
  return true;
}

// Own a bucket while a new chain is staged. No borrowed cursor page survives
// a read; caller data is used only before publication and copied by the batch.
struct Records {
  HashRecord *records = nullptr;
  unsigned char *bytes = nullptr;
  size_t count = 0;
  ~Records() {
    delete[] bytes;
    delete[] records;
  }

  ErrorOr<size_t> load(HashIO io, const HashHeader &header, uint64_t extent,
                       uint32_t bucket) {
    HashChainReader reader(io, header, extent);
    size_t size = 0;
    if (extent) {
      auto start = reader.begin(bucket);
      if (!start)
        return Error(start.error());
      HashRecord record{};
      for (;;) {
        auto next = reader.next(record);
        if (!next)
          return Error(next.error());
        if (!*next)
          break;
        size_t length = record.key_size + record.content_size;
        if (++count > header.keys || length > SIZE_MAX - size)
          return Error(EOVERFLOW);
        size += length;
      }
    }
    if (count >= SIZE_MAX / sizeof(HashRecord))
      return Error(EOVERFLOW);
    AllocChecker ac;
    records = new (ac) HashRecord[count + 1];
    if (!ac)
      return Error(ENOMEM);
    bytes = new (ac) unsigned char[size ? size : 1];
    if (!ac)
      return Error(ENOMEM);
    if (!extent)
      return size_t{0};
    auto start = reader.begin(bucket);
    if (!start)
      return Error(start.error());
    size_t index = 0, offset = 0;
    HashRecord record{};
    for (;;) {
      auto next = reader.next(record);
      if (!next)
        return Error(next.error());
      if (!*next)
        break;
      size_t length = record.key_size + record.content_size;
      if (index == count || length > size - offset)
        return Error(EINVAL);
      records[index++] = {bytes + offset, record.key_size,
                          bytes + offset + record.key_size,
                          record.content_size};
      inline_memcpy(bytes + offset, record.key, record.key_size);
      inline_memcpy(bytes + offset + record.key_size, record.content,
                    record.content_size);
      offset += length;
    }
    if (index != count || offset != size)
      return Error(EINVAL);
    return count;
  }
};
} // namespace

void Database::reset_cursor() {
  delete cursor;
  cursor = nullptr;
}

ErrorOr<size_t> Database::refresh() {
  if (damaged)
    return error(EIO);
  auto state = file.inspect();
  if (!state)
    return error(state.error());
  header = state->header;
  extent = state->extent;
  return size_t{0};
}

ErrorOr<size_t> Database::open(const char *path, int flags,
                               mode_t permissions) {
  auto state = file.open(path, flags, permissions);
  if (!state)
    return error(state.error());
  header = state->header;
  extent = state->extent;
  failure = 0;
  damaged = false;
  reset_cursor();
  return size_t{0};
}

ErrorOr<int> Database::close() {
  reset_cursor();
  auto closed = file.close();
  if (!closed)
    return error(closed.error());
  return 0;
}

ErrorOr<bool> Database::fetch(const void *key, size_t size, HashRecord &out) {
  out = {};
  if (!file.readable())
    return error(EBADF);
  if ((size && !key) || size > HASH_RECORD_LIMIT)
    return error(EINVAL);
  auto current = refresh();
  if (!current)
    return error(current.error());
  if (!extent)
    return false;
  HashChainReader reader(file.io(), header, extent);
  auto begin = reader.begin(
      hash_key_bucket(header, static_cast<const unsigned char *>(key), size));
  if (!begin)
    return error(begin.error());
  HashRecord record{};
  for (;;) {
    auto next = reader.next(record);
    if (!next)
      return error(next.error());
    if (!*next)
      return false;
    if (equal_key(record, key, size)) {
      inline_memcpy(returned, record.key, record.key_size);
      inline_memcpy(returned + record.key_size, record.content,
                    record.content_size);
      out = {returned, record.key_size, returned + record.key_size,
             record.content_size};
      return true;
    }
  }
}

ErrorOr<bool> Database::first(HashRecord &out) {
  reset_cursor();
  out = {};
  if (!file.readable())
    return error(EBADF);
  auto current = refresh();
  if (!current)
    return error(current.error());
  if (!extent)
    return false;
  AllocChecker ac;
  cursor = new (ac) HashChainReader(file.io(), header, extent);
  if (!ac)
    return error(ENOMEM);
  bucket = 0;
  auto begin = cursor->begin(bucket);
  if (!begin) {
    reset_cursor();
    return error(begin.error());
  }
  return next(out);
}

ErrorOr<bool> Database::next(HashRecord &out) {
  out = {};
  if (damaged)
    return error(EIO);
  while (cursor) {
    HashRecord record{};
    auto next = cursor->next(record);
    if (!next) {
      reset_cursor();
      return error(next.error());
    }
    if (*next) {
      inline_memcpy(returned, record.key, record.key_size);
      out = {returned, record.key_size, nullptr, 0};
      return true;
    }
    if (bucket == header.max_bucket) {
      reset_cursor();
      break;
    }
    auto begin = cursor->begin(++bucket);
    if (!begin) {
      reset_cursor();
      return error(begin.error());
    }
  }
  return false;
}

ErrorOr<bool> Database::mutate(const HashRecord &record, bool replace,
                               bool remove) {
  reset_cursor();
  if (!file.writable())
    return error(EBADF);
  if (!hash_record_fits(record.key_size, record.content_size) ||
      (record.key_size && !record.key) ||
      (record.content_size && !record.content))
    return error(EINVAL);
  auto current = refresh();
  if (!current)
    return error(current.error());
  uint32_t selected = hash_key_bucket(header, record.key, record.key_size);
  Records records;
  auto loaded = records.load(file.io(), header, extent, selected);
  if (!loaded)
    return error(loaded.error());
  size_t index = 0;
  while (index < records.count &&
         !equal_key(records.records[index], record.key, record.key_size))
    ++index;
  bool found = index < records.count;
  if (remove) {
    if (!found)
      return false;
    records.records[index] = records.records[--records.count];
  } else {
    if (found && !replace)
      return false;
    records.records[index] = record;
    if (!found)
      ++records.count;
  }

  HashWriteBatch batch(file.io(), extent);
  if (!extent) {
    auto empty = HashFile::prepare_empty(batch);
    if (!empty)
      return error(empty.error());
  }
  auto prepared = rebuild_hash_bucket(batch, header, header, selected,
                                      records.records, records.count);
  if (!prepared)
    return error(prepared.error());
  // Incremental linear hashing: split one bucket when the average record
  // count exceeds the stored fill factor. Overflow chains handle collisions.
  if (!remove && !found &&
      uint64_t(prepared->keys) >
          (uint64_t(prepared->max_bucket) + 1) * prepared->fill_factor) {
    auto split = plan_hash_split(*prepared);
    if (!split)
      return error(split.error());
    Records redistributed;
    loaded = redistributed.load(batch.view(), *prepared, batch.size(),
                                split->old_bucket);
    if (!loaded)
      return error(loaded.error());
    size_t old_count = 0;
    for (size_t i = 0; i < redistributed.count; ++i) {
      auto &entry = redistributed.records[i];
      if (hash_key_bucket(split->header, entry.key, entry.key_size) ==
          split->old_bucket) {
        HashRecord saved = redistributed.records[old_count];
        redistributed.records[old_count++] = entry;
        entry = saved;
      }
    }
    HashHeader before = *prepared;
    prepared =
        rebuild_hash_bucket(batch, before, split->header, split->old_bucket,
                            redistributed.records, old_count);
    if (!prepared)
      return error(prepared.error());
    prepared = rebuild_hash_bucket(batch, before, *prepared, split->new_bucket,
                                   redistributed.records + old_count,
                                   redistributed.count - old_count);
    if (!prepared)
      return error(prepared.error());
  }
  struct Publication {
    HashWriteBatch &batch;
    bool &damaged;
    bool done = false;
    ~Publication() {
      if (!done && batch.write_started())
        damaged = true;
    }
  } publication{batch, damaged};
  auto published = batch.publish(*prepared, file.synchronizer());
  if (!published)
    return error(published.error());
  publication.done = true;
  header = *prepared;
  extent = batch.size();
  return true;
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
