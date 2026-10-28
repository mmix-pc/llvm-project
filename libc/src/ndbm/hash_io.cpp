//===-- Checked I/O for BSD hash database pages
//----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hash_io.h"
#include "hdr/errno_macros.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {
namespace {
constexpr uint64_t MAX_OFFSET = 0x7fffffffffffffff;
bool valid_range(uint64_t offset, size_t size) {
  return offset <= MAX_OFFSET && size <= MAX_OFFSET - offset;
}
bool valid_page(uint32_t physical) {
  return physical && physical <= 0x7fffffff;
}
} // namespace

ErrorOr<size_t> read_hash_block(const HashIO &io, unsigned char *bytes,
                                size_t size, uint64_t offset) {
  if (!valid_range(offset, size))
    return Error(EOVERFLOW);
  if (!size)
    return size_t{0};
  if (!bytes || !io.read_at)
    return Error(EINVAL);
  size_t done = 0;
  while (done < size) {
    auto result =
        io.read_at(io.context, bytes + done, size - done, offset + done);
    if (!result)
      return Error(result.error());
    if (*result == 0 || *result > size - done)
      return Error(EIO);
    done += *result;
  }
  return done;
}

ErrorOr<size_t> write_hash_block(const HashIO &io, const unsigned char *bytes,
                                 size_t size, uint64_t offset) {
  if (!valid_range(offset, size))
    return Error(EOVERFLOW);
  if (!size)
    return size_t{0};
  if (!bytes || !io.write_at)
    return Error(EINVAL);
  size_t done = 0;
  while (done < size) {
    auto result =
        io.write_at(io.context, bytes + done, size - done, offset + done);
    if (!result)
      return Error(result.error());
    if (*result == 0 || *result > size - done)
      return Error(EIO);
    done += *result;
  }
  return done;
}

ErrorOr<HashHeader> read_hash_header(const HashIO &io, uint32_t expected_hash) {
  unsigned char bytes[HASH_HEADER_SIZE];
  auto result = read_hash_block(io, bytes, sizeof(bytes), 0);
  if (!result)
    return Error(result.error());
  HashHeader header{};
  if (!decode_hash_header(bytes, sizeof(bytes), expected_hash, header))
    return Error(EINVAL);
  return header;
}

ErrorOr<size_t> write_hash_header(const HashIO &io, const HashHeader &header,
                                  uint32_t expected_hash) {
  unsigned char bytes[HASH_HEADER_SIZE];
  if (!encode_hash_header(header, expected_hash, bytes, sizeof(bytes)))
    return Error(EINVAL);
  return write_hash_block(io, bytes, sizeof(bytes), 0);
}

ErrorOr<HashPageStatus>
read_hash_data_page(const HashIO &io, uint32_t physical, bool big_endian,
                    unsigned char (&out)[HASH_PAGE_SIZE]) {
  if (!valid_page(physical))
    return Error(EINVAL);
  unsigned char page[HASH_PAGE_SIZE];
  auto result = read_hash_block(io, page, sizeof(page),
                                uint64_t(physical) * HASH_PAGE_SIZE);
  if (!result)
    return Error(result.error());
  HashPageStatus status = validate_hash_page(page, sizeof(page), big_endian);
  if (status == HashPageStatus::Malformed)
    return Error(EINVAL);
  if (status == HashPageStatus::LargeRecord)
    return Error(ENOTSUP);
  // Zero-filled pages remain distinguishable: only the engine can decide
  // whether the page is a legitimate sparse/new allocation.
  inline_memcpy(out, page, sizeof(page));
  return status;
}

ErrorOr<size_t>
write_hash_data_page(const HashIO &io, uint32_t physical, bool big_endian,
                     const unsigned char (&page)[HASH_PAGE_SIZE]) {
  if (!valid_page(physical))
    return Error(EINVAL);
  HashPageStatus status = validate_hash_page(page, sizeof(page), big_endian);
  if (status == HashPageStatus::LargeRecord)
    return Error(ENOTSUP);
  if (status != HashPageStatus::Valid)
    return Error(EINVAL);
  return write_hash_block(io, page, sizeof(page),
                          uint64_t(physical) * HASH_PAGE_SIZE);
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
