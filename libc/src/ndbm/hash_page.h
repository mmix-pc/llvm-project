//===-- Private BSD hash page operations -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_PAGE_H
#define LLVM_LIBC_SRC_NDBM_HASH_PAGE_H

#include "hash_validation.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

struct HashRecord {
  const unsigned char *key;
  size_t key_size;
  const unsigned char *content;
  size_t content_size;
};

struct HashPageInfo {
  size_t records;
  uint16_t overflow;
};

bool read_hash_page_info(const unsigned char *bytes, size_t size,
                         bool big_endian, HashPageInfo &out);

enum class HashPageResult {
  Success,
  Malformed,
  Full,
  RecordTooLarge,
  NotFound
};

bool initialize_hash_page(unsigned char *bytes, size_t size, bool big_endian);

// Returned record storage is borrowed until the next page mutation.
HashPageResult read_hash_record(const unsigned char *bytes, size_t size,
                                bool big_endian, size_t record,
                                HashRecord &out);

// Input record storage must not alias the page. Failures leave the page intact.
HashPageResult append_hash_record(unsigned char *bytes, size_t size,
                                  bool big_endian, const HashRecord &record);
HashPageResult remove_hash_record(unsigned char *bytes, size_t size,
                                  bool big_endian, size_t record);
HashPageResult set_hash_overflow(unsigned char *bytes, size_t size,
                                 bool big_endian, uint16_t address);

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
