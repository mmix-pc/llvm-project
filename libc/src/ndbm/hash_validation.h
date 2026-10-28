//===-- Bounds for the private BSD hash database format ----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_VALIDATION_H
#define LLVM_LIBC_SRC_NDBM_HASH_VALIDATION_H

#include "hdr/stdint_proxy.h"
#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

inline constexpr size_t HASH_PAGE_SIZE = 4096;
inline constexpr size_t HASH_HEADER_SIZE = 260;
inline constexpr size_t HASH_RECORD_LIMIT = HASH_PAGE_SIZE - 14;

// Decoded values, never a structure overlaid on untrusted file bytes.
struct HashHeader {
  uint32_t byte_order;
  uint32_t directory_size;
  uint32_t overflow_point;
  uint32_t last_freed;
  uint32_t max_bucket;
  uint32_t high_mask;
  uint32_t low_mask;
  uint32_t fill_factor;
  uint32_t keys;
  uint32_t spares[32];
  uint16_t bitmaps[32];
};

bool decode_hash_header(const unsigned char *bytes, size_t size,
                        uint32_t expected_hash, HashHeader &out);

// Reject invalid geometry before changing output; the header is big-endian
// independently of the data-page byte order stored in it.
bool encode_hash_header(const HashHeader &header, uint32_t expected_hash,
                        unsigned char *bytes, size_t size);

// Translate an allocated logical address using a decoded header. These do not
// prove that a physical page exists in the file or belongs to a given chain.
bool hash_bucket_page(const HashHeader &header, uint32_t bucket,
                      uint32_t &page);
bool hash_overflow_page(const HashHeader &header, uint16_t address,
                        uint32_t &page);

enum class HashPageStatus { Valid, ZeroFilled, Malformed, LargeRecord };

// Validates a data page, not a bitmap page or a complete overflow chain.
HashPageStatus validate_hash_page(const unsigned char *bytes, size_t size,
                                  bool big_endian);

bool hash_record_fits(size_t key, size_t content);

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
