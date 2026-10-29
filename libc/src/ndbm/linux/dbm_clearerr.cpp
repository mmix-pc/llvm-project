//===-- Linux implementation of dbm_clearerr ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/dbm_clearerr.h"
#include "database.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, dbm_clearerr, (DBM * db)) {
  reinterpret_cast<dbm::Database *>(db)->clear_error();
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
