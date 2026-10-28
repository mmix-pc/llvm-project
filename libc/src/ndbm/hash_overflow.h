//===-- Checked hash overflow allocation -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_OVERFLOW_H
#define LLVM_LIBC_SRC_NDBM_HASH_OVERFLOW_H

#include "hash_io.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

struct HashOverflow {
  HashHeader header;
  uint16_t address;
  uint32_t physical;
  uint32_t bitmap_physical;
  unsigned char bitmap[HASH_PAGE_SIZE];
};

// Prepare, but do not persist, one allocation/release. The owner has validated
// the complete database and excludes concurrent mutation. On release it also
// owns removal of the chain reference. No I/O write or input mutation occurs.
ErrorOr<HashOverflow> plan_hash_overflow(const HashIO &io,
                                         const HashHeader &header);
ErrorOr<HashOverflow>
plan_hash_release(const HashIO &io, const HashHeader &header, uint16_t address);

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
