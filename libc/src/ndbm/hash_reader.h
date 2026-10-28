//===-- Checked hash chain reader -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_READER_H
#define LLVM_LIBC_SRC_NDBM_HASH_READER_H

#include "hash_io.h"
#include "hash_page.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

// The caller owns the header and transport, and prevents file mutation during
// traversal. This cursor validates one chain, not ownership across all buckets.
class HashChainReader {
  HashIO io;
  const HashHeader &header;
  uint64_t file_size;
  uint32_t bucket = 0;
  uint32_t physical = 0;
  unsigned char page[HASH_PAGE_SIZE];
  unsigned char visited[8192];
  HashPageInfo info{};
  size_t record = 0;
  bool loaded = false;
  bool finished = true;
  int failure = 0;

  bool page_exists(uint32_t number) const;
  ErrorOr<size_t> check_overflow(uint16_t address);
  ErrorOr<size_t> load_page(bool primary);
  ErrorOr<bool> fail(int error);

public:
  HashChainReader(HashIO io, const HashHeader &header, uint64_t file_size)
      : io(io), header(header), file_size(file_size) {}
  HashChainReader(const HashChainReader &) = delete;
  HashChainReader &operator=(const HashChainReader &) = delete;

  ErrorOr<size_t> begin(uint32_t bucket);
  // A true result borrows page storage until the next cursor operation.
  // End/error clears out; an error remains sticky until begin is called again.
  ErrorOr<bool> next(HashRecord &out);
};

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
