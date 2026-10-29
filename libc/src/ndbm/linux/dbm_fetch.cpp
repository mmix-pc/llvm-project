//===-- Linux implementation of dbm_fetch ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/dbm_fetch.h"
#include "database.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(datum, dbm_fetch, (DBM * db, datum key)) {
  dbm::HashRecord record{};
  auto found =
      reinterpret_cast<dbm::Database *>(db)->fetch(key.dptr, key.dsize, record);
  if (!found) {
    libc_errno = found.error();
    return {nullptr, 0};
  }
  if (!*found)
    return {nullptr, 0};
  return {const_cast<unsigned char *>(record.content), record.content_size};
}
} // namespace LIBC_NAMESPACE_DECL
