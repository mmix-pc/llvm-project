//===-- Whole-database BSD hash validation ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_DATABASE_H
#define LLVM_LIBC_SRC_NDBM_HASH_DATABASE_H

#include "hash_reader.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

// The owner prevents mutation throughout validation. No record is published
// until geometry, allocation ownership and binary-key uniqueness are checked.
ErrorOr<size_t> validate_hash_database(const HashIO &io,
                                       const HashHeader &header,
                                       uint64_t file_size);

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
