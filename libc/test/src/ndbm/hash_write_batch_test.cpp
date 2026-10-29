//===-- Tests for staged BSD hash writes ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/ndbm/hash_database.h"
#include "src/ndbm/hash_function.h"
#include "src/ndbm/hash_growth.h"
#include "src/ndbm/hash_rebuild.h"
#include "src/ndbm/hash_write_batch.h"
#include "test/UnitTest/Test.h"

namespace db = LIBC_NAMESPACE::dbm;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
bool track;
unsigned allocations, releases, fail_at;

struct Storage {
  unsigned char bytes[8 * db::HASH_PAGE_SIZE]{};
  db::HashHeader header{};
  size_t extent = 4 * db::HASH_PAGE_SIZE;
  size_t reads = 0, writes = 0, syncs = 0, write_limit = SIZE_MAX;
  bool read_error = false, sync_error = false;

  explicit Storage(bool be = true) {
    header.byte_order = be ? 4321 : 1234;
    header.directory_size = 1;
    header.overflow_point = 1;
    header.max_bucket = header.low_mask = 1;
    header.high_mask = 3;
    header.fill_factor = 4;
    header.spares[1] = 1;
    header.bitmaps[0] = 2049;
    EXPECT_TRUE(db::encode_hash_header(header, db::hash_signature(), bytes,
                                       db::HASH_HEADER_SIZE));
    for (unsigned page = 1; page <= 2; ++page)
      EXPECT_TRUE(db::initialize_hash_page(bytes + page * db::HASH_PAGE_SIZE,
                                           db::HASH_PAGE_SIZE, be));
    bytes[3 * db::HASH_PAGE_SIZE + (be ? 3 : 0)] = 1;
  }
  static ErrorOr<size_t> read(void *ctx, unsigned char *out, size_t count,
                              uint64_t offset) {
    auto &s = *static_cast<Storage *>(ctx);
    ++s.reads;
    if (s.read_error)
      return Error(EINTR);
    if (offset >= s.extent)
      return size_t{0};
    if (count > s.extent - offset)
      count = s.extent - offset;
    for (size_t i = 0; i < count; ++i)
      out[i] = s.bytes[offset + i];
    return count;
  }
  static ErrorOr<size_t> write(void *ctx, const unsigned char *in, size_t count,
                               uint64_t offset) {
    auto &s = *static_cast<Storage *>(ctx);
    ++s.writes;
    if (!s.write_limit)
      return Error(ENOSPC);
    if (count > s.write_limit)
      count = s.write_limit;
    if (offset > sizeof(s.bytes) || count > sizeof(s.bytes) - offset)
      return Error(EFBIG);
    for (size_t i = 0; i < count; ++i)
      s.bytes[offset + i] = in[i];
    s.write_limit -= count;
    if (offset + count > s.extent)
      s.extent = offset + count;
    return count;
  }
  static ErrorOr<size_t> sync(void *ctx) {
    auto &s = *static_cast<Storage *>(ctx);
    ++s.syncs;
    // The header must have been published before synchronization.
    auto decoded = db::read_hash_header(s.io(), db::hash_signature());
    EXPECT_TRUE(decoded.has_value());
    if (s.sync_error)
      return Error(EIO);
    return size_t{0};
  }
  db::HashIO io() { return {this, read, write}; }
  void one_record(unsigned char (&page)[db::HASH_PAGE_SIZE]) {
    bool be = header.byte_order == 4321;
    EXPECT_TRUE(db::initialize_hash_page(page, sizeof(page), be));
    db::HashRecord empty{nullptr, 0, nullptr, 0};
    EXPECT_TRUE(db::append_hash_record(page, sizeof(page), be, empty) ==
                db::HashPageResult::Success);
  }
};
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (track && ++allocations == fail_at)
    return nullptr;
  return __real_malloc(size);
}
extern "C" void __wrap_free(void *p) {
  if (track && p)
    ++releases;
  __real_free(p);
}

TEST(LlvmLibcHashWriteBatchTest, StagingIsDiscardableAndCopiesInput) {
  Storage s;
  unsigned char page[db::HASH_PAGE_SIZE], observed[db::HASH_PAGE_SIZE];
  s.one_record(page);
  {
    db::HashWriteBatch batch(s.io(), s.extent);
    ASSERT_TRUE(batch.stage(1, page).has_value());
    page[20] = 17;
    ASSERT_TRUE(db::read_hash_block(batch.view(), observed, sizeof(observed),
                                    db::HASH_PAGE_SIZE)
                    .has_value());
    EXPECT_EQ(observed[20], static_cast<unsigned char>(0));
    ASSERT_TRUE(batch.stage(1, page).has_value());
    ASSERT_TRUE(db::read_hash_block(batch.view(), observed, sizeof(observed),
                                    db::HASH_PAGE_SIZE)
                    .has_value());
    EXPECT_EQ(observed[20], static_cast<unsigned char>(17));
    EXPECT_FALSE(batch.write_started());
  }
  EXPECT_EQ(s.writes, size_t{0});
  auto valid = db::validate_hash_database(s.io(), s.header, s.extent);
  ASSERT_TRUE(valid.has_value());
  EXPECT_EQ(*valid, size_t{0});
}

TEST(LlvmLibcHashWriteBatchTest, AllocationAndValidationFailurePreserveFile) {
  Storage s;
  unsigned char page[db::HASH_PAGE_SIZE];
  s.one_record(page);
  allocations = releases = 0;
  fail_at = 2;
  track = true;
  {
    db::HashWriteBatch batch(s.io(), s.extent);
    ASSERT_TRUE(batch.stage(1, page).has_value());
    auto failed = batch.stage(4, page);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), ENOMEM);
    EXPECT_EQ(batch.size(), uint64_t(s.extent));
    auto invalid = batch.publish(s.header);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), EINVAL);
    EXPECT_FALSE(batch.write_started());
    EXPECT_EQ(s.writes, size_t{0});
    fail_at = 0;
    ++s.header.keys;
    ASSERT_TRUE(batch.publish(s.header).has_value());
  }
  track = false;
  EXPECT_EQ(allocations, 2U);
  EXPECT_EQ(releases, 1U);
}

TEST(LlvmLibcHashWriteBatchTest, PublishOverflowAndRevalidateBothOrders) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage s(be);
    unsigned char primary[db::HASH_PAGE_SIZE], page[db::HASH_PAGE_SIZE];
    s.one_record(page);
    ASSERT_TRUE(db::initialize_hash_page(primary, sizeof(primary), be));
    ASSERT_TRUE(db::set_hash_overflow(primary, sizeof(primary), be, 2050) ==
                db::HashPageResult::Success);
    db::HashWriteBatch batch(s.io(), s.extent);
    ASSERT_TRUE(batch.stage(1, primary).has_value());
    ASSERT_TRUE(batch.stage(4, page).has_value());
    for (auto &byte : page)
      byte = 0xff;
    ASSERT_TRUE(batch.stage(3, page).has_value());
    ++s.header.spares[1];
    ++s.header.keys;
    ASSERT_TRUE(batch.publish(s.header, Storage::sync).has_value());
    EXPECT_TRUE(batch.write_started());
    EXPECT_EQ(s.syncs, size_t{1});
    EXPECT_EQ(s.extent, size_t{5 * db::HASH_PAGE_SIZE});
    auto header = db::read_hash_header(s.io(), db::hash_signature());
    ASSERT_TRUE(header.has_value());
    auto valid = db::validate_hash_database(s.io(), *header, s.extent);
    ASSERT_TRUE(valid.has_value());
    EXPECT_EQ(*valid, size_t{1});
    EXPECT_FALSE(batch.publish(*header).has_value());
    EXPECT_FALSE(batch.stage(1, primary).has_value());
  }
}

TEST(LlvmLibcHashWriteBatchTest, ReadFailureBeforePublicationCanBeRetried) {
  Storage s;
  unsigned char page[db::HASH_PAGE_SIZE];
  s.one_record(page);
  db::HashWriteBatch batch(s.io(), s.extent);
  ASSERT_TRUE(batch.stage(1, page).has_value());
  ++s.header.keys;
  s.read_error = true;
  auto result = batch.publish(s.header, Storage::sync);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINTR);
  EXPECT_FALSE(batch.write_started());
  EXPECT_EQ(s.writes, size_t{0});
  EXPECT_EQ(s.syncs, size_t{0});
  s.read_error = false;
  ASSERT_TRUE(batch.publish(s.header, Storage::sync).has_value());
}

TEST(LlvmLibcHashWriteBatchTest, PartialPageAndHeaderWritesAreNotReplayed) {
  const size_t limits[] = {0, 17, db::HASH_PAGE_SIZE + 17};
  for (size_t limit : limits) {
    Storage s;
    unsigned char page[db::HASH_PAGE_SIZE];
    s.one_record(page);
    db::HashWriteBatch batch(s.io(), s.extent);
    ASSERT_TRUE(batch.stage(1, page).has_value());
    ++s.header.keys;
    s.write_limit = limit;
    auto result = batch.publish(s.header, Storage::sync);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ENOSPC);
    EXPECT_TRUE(batch.write_started());
    size_t writes = s.writes;
    EXPECT_FALSE(batch.publish(s.header, Storage::sync).has_value());
    EXPECT_EQ(s.writes, writes);
    EXPECT_EQ(s.syncs, size_t{0});
    EXPECT_FALSE(db::read_hash_block(batch.view(), page, sizeof(page),
                                     db::HASH_PAGE_SIZE)
                     .has_value());
  }
}

TEST(LlvmLibcHashWriteBatchTest, SynchronizationFailureIsNotSuccess) {
  Storage s;
  db::HashWriteBatch batch(s.io(), s.extent);
  s.sync_error = true;
  auto result = batch.publish(s.header, Storage::sync);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EIO);
  EXPECT_TRUE(batch.write_started());
  EXPECT_EQ(s.syncs, size_t{1});
  EXPECT_FALSE(batch.publish(s.header, Storage::sync).has_value());
  EXPECT_EQ(s.syncs, size_t{1});
}

TEST(LlvmLibcHashWriteBatchTest, RebuildMultipleMaximumRecordsAndReleasePages) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage s(be);
    unsigned char content[4081]{};
    const unsigned char keys[] = {0, 2, 4, 6};
    db::HashRecord records[4];
    for (unsigned i = 0; i < 4; ++i)
      records[i] = {keys + i, 1, content, sizeof(content)};
    {
      db::HashWriteBatch batch(s.io(), s.extent);
      auto rebuilt =
          db::rebuild_hash_bucket(batch, s.header, s.header, 0, records, 4);
      ASSERT_TRUE(rebuilt.has_value());
      EXPECT_EQ(s.writes, size_t{0});
      ASSERT_TRUE(batch.publish(*rebuilt).has_value());
      s.header = *rebuilt;
    }
    EXPECT_EQ(s.header.keys, 4U);
    EXPECT_EQ(s.header.spares[1], 4U);
    size_t extent = s.extent;
    {
      db::HashWriteBatch batch(s.io(), s.extent);
      auto rebuilt =
          db::rebuild_hash_bucket(batch, s.header, s.header, 0, records + 2, 2);
      ASSERT_TRUE(rebuilt.has_value());
      ASSERT_TRUE(batch.publish(*rebuilt).has_value());
      s.header = *rebuilt;
    }
    EXPECT_EQ(s.header.keys, 2U);
    EXPECT_EQ(s.extent, extent);
    auto valid = db::validate_hash_database(s.io(), s.header, s.extent);
    ASSERT_TRUE(valid.has_value());
    EXPECT_EQ(*valid, size_t{2});
    db::HashChainReader reader(s.io(), s.header, s.extent);
    ASSERT_TRUE(reader.begin(0).has_value());
    db::HashRecord record{};
    for (unsigned i = 2; i < 4; ++i) {
      auto next = reader.next(record);
      ASSERT_TRUE(next.has_value());
      ASSERT_TRUE(*next);
      ASSERT_EQ(record.key_size, size_t{1});
      EXPECT_EQ(record.key[0], keys[i]);
      EXPECT_EQ(record.content_size, sizeof(content));
    }
    auto end = reader.next(record);
    ASSERT_TRUE(end.has_value());
    EXPECT_FALSE(*end);
    {
      db::HashWriteBatch batch(s.io(), s.extent);
      auto rebuilt =
          db::rebuild_hash_bucket(batch, s.header, s.header, 0, nullptr, 0);
      ASSERT_TRUE(rebuilt.has_value());
      ASSERT_TRUE(batch.publish(*rebuilt).has_value());
      s.header = *rebuilt;
    }
    valid = db::validate_hash_database(s.io(), s.header, s.extent);
    ASSERT_TRUE(valid.has_value());
    EXPECT_EQ(*valid, size_t{0});
  }
}

TEST(LlvmLibcHashWriteBatchTest, RebuildSplitPublishesBothPartitionsTogether) {
  Storage s;
  unsigned char content[4081]{};
  const unsigned char keys[] = {0, 2, 4, 6};
  db::HashRecord records[4];
  for (unsigned i = 0; i < 4; ++i)
    records[i] = {keys + i, 1, content, sizeof(content)};
  {
    db::HashWriteBatch batch(s.io(), s.extent);
    auto rebuilt =
        db::rebuild_hash_bucket(batch, s.header, s.header, 0, records, 4);
    ASSERT_TRUE(rebuilt.has_value());
    ASSERT_TRUE(batch.publish(*rebuilt).has_value());
    s.header = *rebuilt;
  }
  auto split = db::plan_hash_split(s.header);
  ASSERT_TRUE(split.has_value());
  EXPECT_EQ(split->old_bucket, 0U);
  EXPECT_EQ(split->new_bucket, 2U);
  db::HashRecord old_records[] = {records[0], records[2]};
  db::HashRecord new_records[] = {records[1], records[3]};
  db::HashWriteBatch batch(s.io(), s.extent);
  auto first = db::rebuild_hash_bucket(batch, s.header, split->header, 0,
                                       old_records, 2);
  ASSERT_TRUE(first.has_value());
  auto second =
      db::rebuild_hash_bucket(batch, s.header, *first, 2, new_records, 2);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->keys, 4U);
  ASSERT_TRUE(batch.publish(*second).has_value());
  auto header = db::read_hash_header(s.io(), db::hash_signature());
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->max_bucket, 2U);
  auto valid = db::validate_hash_database(s.io(), *header, s.extent);
  ASSERT_TRUE(valid.has_value());
  EXPECT_EQ(*valid, size_t{4});
}

TEST(LlvmLibcHashWriteBatchTest,
     EveryPreparationAllocationFailurePreservesFile) {
  for (unsigned fail = 1; fail <= 7; ++fail) {
    Storage s;
    unsigned char content[4081]{};
    const unsigned char keys[] = {0, 2, 4, 6};
    db::HashRecord records[4];
    for (unsigned i = 0; i < 4; ++i)
      records[i] = {keys + i, 1, content, sizeof(content)};
    allocations = releases = 0;
    fail_at = fail;
    track = true;
    {
      db::HashWriteBatch batch(s.io(), s.extent);
      auto rebuilt =
          db::rebuild_hash_bucket(batch, s.header, s.header, 0, records, 4);
      if (rebuilt) {
        auto published = batch.publish(*rebuilt);
        ASSERT_FALSE(published.has_value());
        EXPECT_EQ(published.error(), ENOMEM);
      } else {
        EXPECT_EQ(rebuilt.error(), ENOMEM);
      }
      EXPECT_EQ(s.writes, size_t{0});
      EXPECT_FALSE(batch.write_started());
    }
    track = false;
    EXPECT_EQ(releases + 1, allocations);
    auto valid = db::validate_hash_database(s.io(), s.header, s.extent);
    ASSERT_TRUE(valid.has_value());
    EXPECT_EQ(*valid, size_t{0});
  }
}

TEST(LlvmLibcHashWriteBatchTest, InvalidRecordsFailBeforeBackingMutation) {
  Storage s;
  db::HashWriteBatch batch(s.io(), s.extent);
  unsigned char byte = 1;
  db::HashRecord wrong_bucket{&byte, 1, nullptr, 0};
  EXPECT_FALSE(
      db::rebuild_hash_bucket(batch, s.header, s.header, 0, &wrong_bucket, 1)
          .has_value());
  db::HashRecord oversized{&byte, SIZE_MAX, nullptr, 0};
  EXPECT_FALSE(
      db::rebuild_hash_bucket(batch, s.header, s.header, 0, &oversized, 1)
          .has_value());
  db::HashRecord duplicates[] = {{nullptr, 0, nullptr, 0},
                                 {nullptr, 0, nullptr, 0}};
  auto rebuilt =
      db::rebuild_hash_bucket(batch, s.header, s.header, 0, duplicates, 2);
  ASSERT_TRUE(rebuilt.has_value());
  auto published = batch.publish(*rebuilt);
  ASSERT_FALSE(published.has_value());
  EXPECT_EQ(published.error(), EINVAL);
  EXPECT_EQ(s.writes, size_t{0});
}
