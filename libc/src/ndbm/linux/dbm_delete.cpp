//===-- Linux implementation of dbm_delete ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/dbm_delete.h"
#include "database.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, dbm_delete, (DBM * db, datum key)) {
  auto removed =
      reinterpret_cast<dbm::Database *>(db)->erase(key.dptr, key.dsize);
  if (!removed) {
    libc_errno = removed.error();
    return -1;
  }
  return *removed ? 0 : -1;
}
} // namespace LIBC_NAMESPACE_DECL
