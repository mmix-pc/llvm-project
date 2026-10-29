//===-- Rebuild a staged BSD hash bucket -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_REBUILD_H
#define LLVM_LIBC_SRC_NDBM_HASH_REBUILD_H

#include "hash_page.h"
#include "hash_write_batch.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

// Copy caller-owned records into a replacement chain, reusing released pages.
// existing describes the current view; layout may be a checked bucket split.
// It must preserve existing physical addresses and allocation metadata.
// Discard the entire unpublished batch on error; no backing write is performed.
ErrorOr<HashHeader>
rebuild_hash_bucket(HashWriteBatch &batch, const HashHeader &existing,
                    const HashHeader &layout, uint32_t bucket,
                    const HashRecord *records, size_t count);

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_NDBM_HASH_REBUILD_H
