//===-- Tests for checked hash chain traversal
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/ndbm/hash_function.h"
#include "src/ndbm/hash_reader.h"
#include "test/UnitTest/Test.h"

namespace db = LIBC_NAMESPACE::dbm;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
db::HashHeader make_header(bool be) {
  db::HashHeader header{};
  header.byte_order = be ? 4321 : 1234;
  header.directory_size = 256;
  header.overflow_point = 1;
  header.last_freed = 2;
  header.max_bucket = 1;
  header.high_mask = 3;
  header.low_mask = 1;
  header.fill_factor = 4;
  header.keys = 3;
  header.spares[1] = header.spares[2] = 3;
  header.bitmaps[0] = 2049;
  return header;
}

struct Storage {
  unsigned char pages[6][db::HASH_PAGE_SIZE]{};
  size_t length = sizeof(pages), calls = 0;
  uint64_t error_offset = uint64_t(-1);
  int error = EINTR;

  static ErrorOr<size_t> read(void *context, unsigned char *bytes, size_t size,
                              uint64_t offset) {
    auto &storage = *static_cast<Storage *>(context);
    ++storage.calls;
    if (offset == storage.error_offset)
      return Error(storage.error);
    if (offset >= storage.length)
      return size_t{0};
    if (size > storage.length - offset)
      size = storage.length - offset;
    for (size_t i = 0; i < size; ++i)
      bytes[i] = storage.pages[(offset + i) / db::HASH_PAGE_SIZE]
                              [(offset + i) % db::HASH_PAGE_SIZE];
    return size;
  }
  db::HashIO io() { return {this, read, nullptr}; }

  void bitmap(uint32_t bits, bool be) {
    for (unsigned i = 0; i < 4; ++i)
      pages[3][i] =
          static_cast<unsigned char>(bits >> (be ? 24 - i * 8 : i * 8));
  }
  void initialize(bool be, bool records = true) {
    const unsigned physical[] = {1, 2, 4, 5};
    for (unsigned p : physical)
      ASSERT_TRUE(db::initialize_hash_page(pages[p], db::HASH_PAGE_SIZE, be));
    bitmap(7, be); // Bitmap itself, overflow addresses 2050 and 2051.
    ASSERT_TRUE(db::set_hash_overflow(pages[1], db::HASH_PAGE_SIZE, be, 2050) ==
                db::HashPageResult::Success);
    ASSERT_TRUE(db::set_hash_overflow(pages[4], db::HASH_PAGE_SIZE, be, 2051) ==
                db::HashPageResult::Success);
    if (records) {
      const unsigned char keys[] = {'b', 'd', 'f'};
      const unsigned char value[] = {0, 255, 128};
      const unsigned destinations[] = {1, 4, 5};
      for (unsigned i = 0; i < 3; ++i) {
        db::HashRecord entry{keys + i, 1, value,
                             i == 1 ? size_t{0} : sizeof(value)};
        ASSERT_TRUE(db::append_hash_record(pages[destinations[i]],
                                           db::HASH_PAGE_SIZE, be, entry) ==
                    db::HashPageResult::Success);
      }
    }
  }
};
} // namespace

TEST(LlvmLibcHashReaderTest, HashFunctionAndBucketSplit) {
  const unsigned char abc[] = {'a', 'b', 'c'};
  const unsigned char binary[] = {0, 255, 128, 1};
  EXPECT_EQ(db::hash_key(nullptr, 0), uint32_t{0});
  EXPECT_EQ(db::hash_key(abc, sizeof(abc)), uint32_t{0x1a9a6});
  EXPECT_EQ(db::hash_key(binary, sizeof(binary)), uint32_t{0x44d40});
  EXPECT_EQ(db::hash_signature(), uint32_t{0x956e7de3});
  auto header = make_header(true);
  EXPECT_EQ(db::hash_key_bucket(header, abc + 1, 1), uint32_t{0});
  EXPECT_EQ(db::hash_key_bucket(header, abc + 2, 1), uint32_t{1});
  header.max_bucket = 2;
  header.overflow_point = 2;
  EXPECT_EQ(db::hash_key_bucket(header, abc + 1, 1), uint32_t{2});
  EXPECT_EQ(db::hash_key_bucket(header, abc + 2, 1), uint32_t{1});
}

TEST(LlvmLibcHashReaderTest, TraverseBothPageOrders) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage storage;
    storage.initialize(be);
    auto header = make_header(be);
    db::HashChainReader reader(storage.io(), header, storage.length);
    ASSERT_TRUE(reader.begin(0).has_value());
    const unsigned char keys[] = {'b', 'd', 'f'};
    db::HashRecord record{};
    for (unsigned i = 0; i < 3; ++i) {
      auto result = reader.next(record);
      ASSERT_TRUE(result.has_value());
      ASSERT_TRUE(*result);
      ASSERT_EQ(record.key_size, size_t{1});
      EXPECT_EQ(record.key[0], keys[i]);
      EXPECT_EQ(record.content_size, i == 1 ? size_t{0} : size_t{3});
      if (record.content_size) {
        EXPECT_EQ(record.content[0], static_cast<unsigned char>(0));
        EXPECT_EQ(record.content[1], static_cast<unsigned char>(255));
        EXPECT_EQ(record.content[2], static_cast<unsigned char>(128));
      }
    }
    auto result = reader.next(record);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_EQ(record.key, nullptr);
    EXPECT_EQ(record.content, nullptr);
    EXPECT_EQ(record.key_size, size_t{0});
    size_t calls = storage.calls;
    EXPECT_FALSE(*reader.next(record));
    EXPECT_EQ(storage.calls, calls);
    ASSERT_TRUE(reader.begin(1).has_value());
    EXPECT_FALSE(*reader.next(record));
  }
}

TEST(LlvmLibcHashReaderTest, UnallocatedPageAndStickyState) {
  Storage storage;
  storage.initialize(true);
  storage.bitmap(1, true);
  auto header = make_header(true);
  db::HashChainReader reader(storage.io(), header, storage.length);
  ASSERT_TRUE(reader.begin(0).has_value());
  db::HashRecord record{};
  ASSERT_TRUE(*reader.next(record));
  auto result = reader.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(record.key, nullptr);
  storage.bitmap(7, true);
  size_t calls = storage.calls;
  result = reader.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(storage.calls, calls);
  ASSERT_TRUE(reader.begin(0).has_value());
  for (unsigned i = 0; i < 3; ++i)
    ASSERT_TRUE(*reader.next(record));
  EXPECT_FALSE(*reader.next(record));
}

TEST(LlvmLibcHashReaderTest, CyclesAndBitmapAliasing) {
  const uint16_t links[] = {2050, 2051, 2049};
  for (uint16_t link : links) {
    Storage storage;
    storage.initialize(true, false);
    ASSERT_TRUE(db::set_hash_overflow(storage.pages[5], db::HASH_PAGE_SIZE,
                                      true,
                                      link) == db::HashPageResult::Success);
    auto header = make_header(true);
    db::HashChainReader reader(storage.io(), header, storage.length);
    ASSERT_TRUE(reader.begin(0).has_value());
    db::HashRecord record{};
    auto result = reader.next(record);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EINVAL);
    EXPECT_EQ(record.key, nullptr);
    EXPECT_LE(storage.calls, size_t{5});
  }
}

TEST(LlvmLibcHashReaderTest, TruncationIsNotEmpty) {
  Storage storage;
  auto header = make_header(true);
  db::HashRecord record{};
  db::HashChainReader short_file(storage.io(), header, db::HASH_PAGE_SIZE);
  auto begin = short_file.begin(0);
  ASSERT_FALSE(begin.has_value());
  EXPECT_EQ(begin.error(), EIO);
  EXPECT_EQ(storage.calls, size_t{0});
  db::HashChainReader reader(storage.io(), header, storage.length);
  ASSERT_TRUE(reader.begin(0).has_value());
  auto result = reader.next(record);
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(*result); // Fully present sparse primary bucket.
  storage.length = 2 * db::HASH_PAGE_SIZE - 1;
  ASSERT_TRUE(reader.begin(0).has_value());
  result = reader.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EIO);
  EXPECT_EQ(record.key, nullptr);
}

TEST(LlvmLibcHashReaderTest, ZeroOverflowAndWrongBucket) {
  Storage storage;
  storage.initialize(true, false);
  for (auto &byte : storage.pages[4])
    byte = 0;
  auto header = make_header(true);
  db::HashChainReader reader(storage.io(), header, storage.length);
  ASSERT_TRUE(reader.begin(0).has_value());
  db::HashRecord record{};
  auto result = reader.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  ASSERT_TRUE(
      db::initialize_hash_page(storage.pages[1], db::HASH_PAGE_SIZE, true));
  const unsigned char key = 'c';
  db::HashRecord wrong{&key, 1, nullptr, 0};
  ASSERT_TRUE(db::append_hash_record(storage.pages[1], db::HASH_PAGE_SIZE, true,
                                     wrong) == db::HashPageResult::Success);
  ASSERT_TRUE(reader.begin(0).has_value());
  result = reader.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(record.key, nullptr);
}

TEST(LlvmLibcHashReaderTest, MissingBitmapAndTransportError) {
  Storage storage;
  storage.initialize(true, false);
  auto header = make_header(true);
  db::HashChainReader short_file(storage.io(), header, 3 * db::HASH_PAGE_SIZE);
  ASSERT_TRUE(short_file.begin(0).has_value());
  db::HashRecord record{};
  auto result = short_file.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EIO);
  EXPECT_EQ(storage.calls, size_t{1});
  storage.error_offset = 3 * db::HASH_PAGE_SIZE;
  db::HashChainReader reader(storage.io(), header, storage.length);
  ASSERT_TRUE(reader.begin(0).has_value());
  result = reader.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINTR);
  size_t calls = storage.calls;
  result = reader.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINTR);
  EXPECT_EQ(storage.calls, calls);
}

TEST(LlvmLibcHashReaderTest, RejectBadMetadataBeforeTransport) {
  Storage storage;
  auto header = make_header(true);
  db::HashChainReader reader(storage.io(), header, storage.length);
  EXPECT_FALSE(reader.begin(2).has_value());
  header.overflow_point = 32;
  auto result = reader.begin(0);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(storage.calls, size_t{0});
}

TEST(LlvmLibcHashReaderTest, InvalidAddressBeforeBitmapRead) {
  Storage storage;
  storage.initialize(true, false);
  // A syntactically valid address beyond this header's allocation range.
  ASSERT_TRUE(db::set_hash_overflow(storage.pages[1], db::HASH_PAGE_SIZE, true,
                                    2052) == db::HashPageResult::Success);
  auto header = make_header(true);
  db::HashChainReader reader(storage.io(), header, storage.length);
  ASSERT_TRUE(reader.begin(0).has_value());
  db::HashRecord record{};
  auto result = reader.next(record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(storage.calls, size_t{1});
  EXPECT_EQ(record.key, nullptr);
}

TEST(LlvmLibcHashReaderTest, BitmapWordAndPageSelection) {
  struct SparseStorage {
    unsigned char primary[db::HASH_PAGE_SIZE];
    unsigned char overflow[db::HASH_PAGE_SIZE];
    unsigned char word[4]{};
    uint64_t bitmap_offset = 0, overflow_offset = 0;
    size_t bitmap_reads = 0;
    static ErrorOr<size_t> read(void *context, unsigned char *bytes,
                                size_t size, uint64_t offset) {
      auto &s = *static_cast<SparseStorage *>(context);
      const unsigned char *source;
      if (offset == db::HASH_PAGE_SIZE && size == db::HASH_PAGE_SIZE)
        source = s.primary;
      else if (offset == s.overflow_offset && size == db::HASH_PAGE_SIZE)
        source = s.overflow;
      else if (offset == s.bitmap_offset && size == sizeof(s.word)) {
        source = s.word;
        ++s.bitmap_reads;
      } else
        return Error(EIO);
      for (size_t i = 0; i < size; ++i)
        bytes[i] = source[i];
      return size;
    }
  };
  const bool orders[] = {false, true};
  const uint16_t addresses[] = {33, uint16_t((16U << 11) | 18)};
  for (bool be : orders) {
    for (uint16_t address : addresses) {
      auto header = make_header(be);
      header.overflow_point = 16;
      header.max_bucket = header.low_mask = 65535;
      header.high_mask = 131071;
      for (unsigned i = 0; i < 16; ++i)
        header.spares[i] = (i + 1) * 2047;
      header.spares[16] = 32770;
      header.bitmaps[0] = 1;
      header.bitmaps[1] = (16U << 11) | 17;
      SparseStorage storage;
      ASSERT_TRUE(
          db::initialize_hash_page(storage.primary, db::HASH_PAGE_SIZE, be));
      ASSERT_TRUE(
          db::initialize_hash_page(storage.overflow, db::HASH_PAGE_SIZE, be));
      ASSERT_TRUE(db::set_hash_overflow(storage.primary, db::HASH_PAGE_SIZE, be,
                                        address) ==
                  db::HashPageResult::Success);
      uint32_t physical, bitmap;
      ASSERT_TRUE(db::hash_overflow_page(header, address, physical));
      unsigned split = address >> 11;
      uint32_t bit =
          (split ? header.spares[split - 1] : 0) + (address & 0x7ff) - 1;
      ASSERT_TRUE(
          db::hash_overflow_page(header, header.bitmaps[bit / 32768], bitmap));
      storage.overflow_offset = uint64_t(physical) * db::HASH_PAGE_SIZE;
      storage.bitmap_offset =
          uint64_t(bitmap) * db::HASH_PAGE_SIZE + ((bit % 32768) / 32) * 4;
      uint32_t bits = uint32_t{1} << (bit % 32);
      for (unsigned i = 0; i < 4; ++i)
        storage.word[i] =
            static_cast<unsigned char>(bits >> (be ? 24 - 8 * i : 8 * i));
      // An empty key hashes to bucket zero even in this expanded directory.
      const unsigned char value = 255;
      db::HashRecord entry{nullptr, 0, &value, 1};
      ASSERT_TRUE(db::append_hash_record(storage.overflow, db::HASH_PAGE_SIZE,
                                         be,
                                         entry) == db::HashPageResult::Success);
      db::HashChainReader reader({&storage, SparseStorage::read, nullptr},
                                 header,
                                 uint64_t(physical + 1) * db::HASH_PAGE_SIZE);
      ASSERT_TRUE(reader.begin(0).has_value());
      db::HashRecord record{};
      auto result = reader.next(record);
      ASSERT_TRUE(result.has_value());
      ASSERT_TRUE(*result);
      ASSERT_EQ(record.content_size, size_t{1});
      EXPECT_EQ(record.content[0], value);
      EXPECT_EQ(storage.bitmap_reads, size_t{1});
      EXPECT_FALSE(*reader.next(record));
      for (auto &byte : storage.word)
        byte = 0;
      ASSERT_TRUE(reader.begin(0).has_value());
      result = reader.next(record);
      ASSERT_FALSE(result.has_value());
      EXPECT_EQ(result.error(), EINVAL);
      EXPECT_EQ(record.content, nullptr);
    }
  }
}

TEST(LlvmLibcHashReaderTest, ValidateWholeFileBothOrders) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage storage;
    storage.initialize(be);
    auto header = make_header(be);
    db::HashChainReader reader(storage.io(), header, storage.length);
    auto result = reader.validate_file();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, size_t{3});
    db::HashRecord record{};
    EXPECT_FALSE(*reader.next(record));
    // Validation does not prevent restarting an ordinary bucket traversal.
    ASSERT_TRUE(reader.begin(0).has_value());
    EXPECT_TRUE(*reader.next(record));
    EXPECT_TRUE(reader.validate_file().has_value());
  }
}

TEST(LlvmLibcHashReaderTest, RejectSharedOverflowEvenWithoutRecords) {
  Storage storage;
  storage.initialize(true, false);
  ASSERT_TRUE(db::set_hash_overflow(storage.pages[2], db::HASH_PAGE_SIZE, true,
                                    2050) == db::HashPageResult::Success);
  auto header = make_header(true);
  header.keys = 0;
  db::HashChainReader reader(storage.io(), header, storage.length);
  auto result = reader.validate_file();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  // Each chain is valid separately, but the second chain has no unique owner.
  ASSERT_TRUE(reader.begin(1).has_value());
  db::HashRecord record{};
  EXPECT_FALSE(*reader.next(record));
}

TEST(LlvmLibcHashReaderTest, RejectOrphanAndUnallocatedBitmap) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage storage;
    storage.initialize(be, false);
    ASSERT_TRUE(
        db::initialize_hash_page(storage.pages[4], db::HASH_PAGE_SIZE, be));
    auto header = make_header(be);
    header.keys = 0;
    db::HashChainReader reader(storage.io(), header, storage.length);
    auto result = reader.validate_file();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EINVAL); // Allocated 2051 has no chain owner.
    storage.bitmap(3, be);
    ASSERT_TRUE(reader.validate_file().has_value());
    storage.bitmap(2, be);
    result = reader.validate_file();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              EINVAL); // Bitmap's own allocation bit is missing.
    db::HashRecord record{};
    result = reader.validate_file();
    EXPECT_FALSE(result.has_value());
    size_t calls = storage.calls;
    auto next = reader.next(record);
    ASSERT_FALSE(next.has_value());
    EXPECT_EQ(next.error(), EINVAL);
    EXPECT_EQ(storage.calls, calls);
  }
}

TEST(LlvmLibcHashReaderTest, RejectRecordCountMismatch) {
  const uint32_t counts[] = {2, 4};
  for (uint32_t count : counts) {
    Storage storage;
    storage.initialize(true);
    auto header = make_header(true);
    header.keys = count;
    db::HashChainReader reader(storage.io(), header, storage.length);
    auto result = reader.validate_file();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EINVAL);
  }
}

TEST(LlvmLibcHashReaderTest, WholeFileBitmapFailureAndReservedBits) {
  Storage storage;
  storage.initialize(true, false);
  ASSERT_TRUE(
      db::initialize_hash_page(storage.pages[1], db::HASH_PAGE_SIZE, true));
  auto header = make_header(true);
  header.keys = 0;
  storage.bitmap(0xfffffff9,
                 true); // Only the bitmap is owned; tail is reserved.
  db::HashChainReader reader(storage.io(), header, storage.length);
  ASSERT_TRUE(reader.validate_file().has_value());
  storage.error_offset = 3 * db::HASH_PAGE_SIZE;
  auto result = reader.validate_file();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINTR);
  storage.error_offset = uint64_t(-1);
  storage.length = 3 * db::HASH_PAGE_SIZE + 4;
  result = reader.validate_file();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(),
            EIO); // A present first word is not a complete bitmap.
}

TEST(LlvmLibcHashReaderTest, WholeFileExtentBeforeTraversal) {
  Storage storage;
  auto header = make_header(true);
  header.keys = 0;
  const size_t lengths[] = {db::HASH_PAGE_SIZE, sizeof(storage.pages) - 1};
  for (size_t length : lengths) {
    db::HashChainReader reader(storage.io(), header, length);
    auto result = reader.validate_file();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EIO);
    EXPECT_EQ(storage.calls, size_t{0});
  }
}
