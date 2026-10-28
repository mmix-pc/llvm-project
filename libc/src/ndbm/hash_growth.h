//===-- Checked BSD hash bucket growth ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_GROWTH_H
#define LLVM_LIBC_SRC_NDBM_HASH_GROWTH_H

#include "hash_validation.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

struct HashSplit {
  HashHeader header;
  uint32_t old_bucket;
  uint32_t new_bucket;
  uint32_t new_page;
};

// Plan without changing the original header. The owner must prepare storage
// and redistribute the old bucket before publishing the new metadata.
ErrorOr<HashSplit> plan_hash_split(const HashHeader &header);

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_NDBM_HASH_GROWTH_H
