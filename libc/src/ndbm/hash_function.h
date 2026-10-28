//===-- BSD hash function ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_FUNCTION_H
#define LLVM_LIBC_SRC_NDBM_HASH_FUNCTION_H

#include "hash_validation.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

// Bytes must cover size readable bytes; null is accepted only for size zero.
uint32_t hash_key(const unsigned char *bytes, size_t size);
uint32_t hash_key_bucket(const HashHeader &header, const unsigned char *bytes,
                         size_t size);
uint32_t hash_signature();

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
