//===-- Linux implementation of dbm_store ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/dbm_store.h"
#include "database.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, dbm_store,
                   (DBM * db, datum key, datum content, int mode)) {
  auto &database = *reinterpret_cast<dbm::Database *>(db);
  if (mode != 0 && mode != 1) {
    database.record_error(EINVAL);
    libc_errno = EINVAL;
    return -1;
  }
  dbm::HashRecord record{
      static_cast<const unsigned char *>(key.dptr), key.dsize,
      static_cast<const unsigned char *>(content.dptr), content.dsize};
  auto stored = database.store(record, mode == 1);
  if (!stored) {
    libc_errno = stored.error();
    return -1;
  }
  return *stored ? 0 : 1;
}
} // namespace LIBC_NAMESPACE_DECL
