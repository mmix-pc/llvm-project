//===-- Tests for checked hash database I/O -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/ndbm/hash_io.h"
#include "src/ndbm/hash_page.h"
#include "test/UnitTest/Test.h"

namespace db = LIBC_NAMESPACE::dbm;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
struct Storage {
  unsigned char bytes[2 * db::HASH_PAGE_SIZE]{};
  size_t length = sizeof(bytes), chunk = 7, calls = 0, error_call = 0;
  int error = EIO;
  bool zero = false, excessive = false;
  uint64_t next_offset = 0;
  bool consecutive = true;

  ErrorOr<size_t> transfer(size_t count, uint64_t offset) {
    if (calls)
      consecutive &= offset == next_offset;
    ++calls;
    if (calls == error_call)
      return Error(error);
    if (excessive)
      return count + 1;
    if (zero || offset >= length)
      return size_t{0};
    size_t amount = length - static_cast<size_t>(offset);
    if (amount > count)
      amount = count;
    if (amount > chunk)
      amount = chunk;
    next_offset = offset + amount;
    return amount;
  }
  static ErrorOr<size_t> read(void *context, unsigned char *out, size_t size,
                              uint64_t offset) {
    auto &storage = *static_cast<Storage *>(context);
    auto result = storage.transfer(size, offset);
    if (result && *result <= size)
      for (size_t i = 0; i < *result; ++i)
        out[i] = storage.bytes[offset + i];
    return result;
  }
  static ErrorOr<size_t> write(void *context, const unsigned char *in,
                               size_t size, uint64_t offset) {
    auto &storage = *static_cast<Storage *>(context);
    auto result = storage.transfer(size, offset);
    if (result && *result <= size)
      for (size_t i = 0; i < *result; ++i)
        storage.bytes[offset + i] = in[i];
    return result;
  }
  db::HashIO io() { return {this, read, write}; }
};

bool all(const unsigned char *bytes, size_t size, unsigned char value) {
  for (size_t i = 0; i < size; ++i)
    if (bytes[i] != value)
      return false;
  return true;
}

void word(unsigned char *out, size_t index, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[4 * index + i] = static_cast<unsigned char>(value >> (24 - i * 8));
}
} // namespace

TEST(LlvmLibcHashIOTest, ExactShortTransfers) {
  Storage storage;
  unsigned char source[31], out[31]{};
  for (size_t i = 0; i < sizeof(source); ++i)
    source[i] = static_cast<unsigned char>(i);
  auto written = db::write_hash_block(storage.io(), source, sizeof(source), 5);
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, sizeof(source));
  EXPECT_EQ(storage.calls, size_t{5});
  EXPECT_TRUE(storage.consecutive);
  storage.calls = 0;
  auto read = db::read_hash_block(storage.io(), out, sizeof(out), 5);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, sizeof(out));
  EXPECT_EQ(storage.calls, size_t{5});
  EXPECT_TRUE(storage.consecutive);
  for (size_t i = 0; i < sizeof(out); ++i)
    EXPECT_EQ(out[i], source[i]);
}

TEST(LlvmLibcHashIOTest, FailuresPreserveProgressAndErrors) {
  const int errors[] = {EIO, EINTR, ENOSPC};
  for (int error : errors) {
    Storage storage;
    storage.error_call = 2;
    storage.error = error;
    unsigned char source[21];
    for (auto &byte : source)
      byte = 0x5a;
    auto written =
        db::write_hash_block(storage.io(), source, sizeof(source), 0);
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error(), error);
    EXPECT_EQ(storage.calls, size_t{2});
    EXPECT_TRUE(storage.consecutive);
    EXPECT_TRUE(all(storage.bytes, 7, 0x5a));
    EXPECT_TRUE(all(storage.bytes + 7, sizeof(source) - 7, 0));
    storage.calls = 0;
    unsigned char out[21]{};
    auto read = db::read_hash_block(storage.io(), out, sizeof(out), 0);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error(), error);
    EXPECT_EQ(storage.calls, size_t{2});
    EXPECT_TRUE(all(out, 7, 0x5a));
    EXPECT_TRUE(all(out + 7, sizeof(out) - 7, 0));
  }
}

TEST(LlvmLibcHashIOTest, ZeroProgressAndInvalidCounts) {
  const bool modes[] = {false, true};
  for (bool excessive : modes) {
    Storage storage;
    storage.zero = !excessive;
    storage.excessive = excessive;
    unsigned char byte = 0;
    auto read = db::read_hash_block(storage.io(), &byte, 1, 0);
    auto written = db::write_hash_block(storage.io(), &byte, 1, 0);
    ASSERT_FALSE(read.has_value());
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(read.error(), EIO);
    EXPECT_EQ(written.error(), EIO);
    EXPECT_EQ(storage.calls, size_t{2});
  }
}

TEST(LlvmLibcHashIOTest, RangeAndEmptyRequests) {
  Storage storage;
  unsigned char byte = 0;
  auto read = db::read_hash_block(storage.io(), &byte, 2, 0x7ffffffffffffffe);
  auto written = db::write_hash_block(storage.io(), &byte, 1, uint64_t(-1));
  ASSERT_FALSE(read.has_value());
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(read.error(), EOVERFLOW);
  EXPECT_EQ(written.error(), EOVERFLOW);
  EXPECT_EQ(storage.calls, size_t{0});
  db::HashIO absent{nullptr, nullptr, nullptr};
  EXPECT_TRUE(db::read_hash_block(absent, nullptr, 0, 0).has_value());
  EXPECT_TRUE(db::write_hash_block(absent, nullptr, 0, 0).has_value());
  read = db::read_hash_block(absent, &byte, 1, 0);
  written = db::write_hash_block(absent, &byte, 1, 0);
  EXPECT_EQ(read.error(), EINVAL);
  EXPECT_EQ(written.error(), EINVAL);
}

TEST(LlvmLibcHashIOTest, DataPagePublication) {
  const bool orders[] = {false, true};
  for (bool be : orders) {
    Storage storage;
    unsigned char page[db::HASH_PAGE_SIZE], out[db::HASH_PAGE_SIZE];
    ASSERT_TRUE(db::initialize_hash_page(page, sizeof(page), be));
    ASSERT_TRUE(
        db::write_hash_data_page(storage.io(), 1, be, page).has_value());
    storage.calls = 0;
    auto result = db::read_hash_data_page(storage.io(), 1, be, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result == db::HashPageStatus::Valid);
    for (size_t i = 0; i < sizeof(page); ++i)
      EXPECT_EQ(out[i], page[i]);

    for (auto &byte : out)
      byte = 0xa5;
    storage.calls = 0;
    storage.length = sizeof(storage.bytes) - 1;
    result = db::read_hash_data_page(storage.io(), 1, be, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EIO);
    EXPECT_TRUE(all(out, sizeof(out), 0xa5));
    storage.length = sizeof(storage.bytes);
    storage.calls = 0;
    storage.error_call = 2;
    storage.error = EINTR;
    result = db::read_hash_data_page(storage.io(), 1, be, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EINTR);
    EXPECT_EQ(storage.calls, size_t{2});
    EXPECT_TRUE(all(out, sizeof(out), 0xa5));
    storage.error_call = 0;
    storage.bytes[db::HASH_PAGE_SIZE] = 0xff;
    result = db::read_hash_data_page(storage.io(), 1, be, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EINVAL);
    EXPECT_TRUE(all(out, sizeof(out), 0xa5));
  }
}

TEST(LlvmLibcHashIOTest, SparseIsNotInitializedAndRejectBeforeWrite) {
  Storage storage;
  unsigned char page[db::HASH_PAGE_SIZE]{};
  auto result = db::read_hash_data_page(storage.io(), 1, true, page);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(*result == db::HashPageStatus::ZeroFilled);
  storage.calls = 0;
  auto written = db::write_hash_data_page(storage.io(), 1, true, page);
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), EINVAL);
  result = db::read_hash_data_page(storage.io(), 0, true, page);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  result = db::read_hash_data_page(storage.io(), 0x80000000, true, page);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  EXPECT_EQ(storage.calls, size_t{0});
}

TEST(LlvmLibcHashIOTest, LargeRecordsAreNotPublishedOrWritten) {
  Storage storage;
  unsigned char page[db::HASH_PAGE_SIZE]{}, out[db::HASH_PAGE_SIZE];
  // Big-endian count=2, first key offset=4090, large-record opcode=1.
  page[1] = 2;
  page[2] = 0x0f;
  page[3] = 0xfa;
  page[5] = 1;
  page[6] = 0x0f;
  page[7] = 0xe6;
  page[8] = 0x0f;
  page[9] = 0xf0;
  for (size_t i = 0; i < sizeof(page); ++i) {
    storage.bytes[db::HASH_PAGE_SIZE + i] = page[i];
    out[i] = 0xa5;
  }
  auto written = db::write_hash_data_page(storage.io(), 1, true, page);
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), ENOTSUP);
  EXPECT_EQ(storage.calls, size_t{0});
  auto result = db::read_hash_data_page(storage.io(), 1, true, out);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ENOTSUP);
  EXPECT_TRUE(all(out, sizeof(out), 0xa5));
}

TEST(LlvmLibcHashIOTest, HeaderRequiresCompleteValidatedInput) {
  Storage storage;
  const uint32_t words[] = {0x061561, 2, 4321, 4096, 12, 256, 256, 8, 1, 2,
                            1,        3, 1,    40,   0,  1,   123, 0, 2, 2};
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
    word(storage.bytes, i, words[i]);
  storage.bytes[196] = 8;
  storage.bytes[197] = 1;
  auto result = db::read_hash_header(storage.io(), 123);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->byte_order, uint32_t{4321});
  EXPECT_EQ(result->max_bucket, uint32_t{1});
  Storage output;
  auto written = db::write_hash_header(output.io(), *result, 123);
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, db::HASH_HEADER_SIZE);
  EXPECT_TRUE(output.consecutive);
  for (size_t i = 0; i < db::HASH_HEADER_SIZE; ++i)
    EXPECT_EQ(output.bytes[i], storage.bytes[i]);
  output.calls = 0;
  output.error_call = 2;
  output.error = ENOSPC;
  written = db::write_hash_header(output.io(), *result, 123);
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), ENOSPC);
  EXPECT_EQ(output.calls, size_t{2});
  output.calls = 0;
  result->fill_factor = 0;
  written = db::write_hash_header(output.io(), *result, 123);
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), EINVAL);
  EXPECT_EQ(output.calls, size_t{0});
  result = db::read_hash_header(storage.io(), 124);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  storage.length = db::HASH_HEADER_SIZE - 1;
  result = db::read_hash_header(storage.io(), 123);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EIO);
  storage.calls = 0;
  storage.error_call = 1;
  storage.error = EINTR;
  result = db::read_hash_header(storage.io(), 123);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINTR);
  EXPECT_EQ(storage.calls, size_t{1});
}
