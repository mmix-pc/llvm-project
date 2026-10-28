//===-- Checked BSD hash I/O -------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_IO_H
#define LLVM_LIBC_SRC_NDBM_HASH_IO_H

#include "hash_validation.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

// The file backend and fault-injection tests share these positional operations.
// Callbacks return an actual transfer count or a positive error number.
struct HashIO {
  void *context;
  ErrorOr<size_t> (*read_at)(void *, unsigned char *, size_t, uint64_t);
  ErrorOr<size_t> (*write_at)(void *, const unsigned char *, size_t, uint64_t);
};

// On error a prefix may have been transferred; no rollback or EINTR retry.
// The range's exclusive end must fit in a signed 64-bit file offset.
ErrorOr<size_t> read_hash_block(const HashIO &io, unsigned char *bytes,
                                size_t size, uint64_t offset);
ErrorOr<size_t> write_hash_block(const HashIO &io, const unsigned char *bytes,
                                 size_t size, uint64_t offset);

// Publish decoded data only after a complete read and successful validation.
ErrorOr<HashHeader> read_hash_header(const HashIO &io, uint32_t expected_hash);
ErrorOr<size_t> write_hash_header(const HashIO &io, const HashHeader &header,
                                  uint32_t expected_hash);
ErrorOr<HashPageStatus>
read_hash_data_page(const HashIO &io, uint32_t physical, bool big_endian,
                    unsigned char (&out)[HASH_PAGE_SIZE]);
ErrorOr<size_t>
write_hash_data_page(const HashIO &io, uint32_t physical, bool big_endian,
                     const unsigned char (&page)[HASH_PAGE_SIZE]);

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
