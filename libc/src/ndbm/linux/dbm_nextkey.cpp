//===-- Linux implementation of dbm_nextkey ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/dbm_nextkey.h"
#include "database.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(datum, dbm_nextkey, (DBM * db)) {
  dbm::HashRecord record{};
  auto found = reinterpret_cast<dbm::Database *>(db)->next(record);
  if (!found) {
    libc_errno = found.error();
    return {nullptr, 0};
  }
  if (!*found)
    return {nullptr, 0};
  return {const_cast<unsigned char *>(record.key), record.key_size};
}
} // namespace LIBC_NAMESPACE_DECL
