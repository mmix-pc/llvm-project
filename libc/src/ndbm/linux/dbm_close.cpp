//===-- Linux implementation of dbm_close ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/dbm_close.h"
#include "database.h"
#include "src/__support/CPP/new.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, dbm_close, (DBM * db)) {
  auto *database = reinterpret_cast<dbm::Database *>(db);
  auto closed = database->close();
  delete database;
  if (!closed)
    libc_errno = closed.error();
}
} // namespace LIBC_NAMESPACE_DECL
