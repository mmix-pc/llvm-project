//===-- Linux implementation of dbm_open ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ndbm/dbm_open.h"
#include "database.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(DBM *, dbm_open,
                   (const char *path, int flags, mode_t mode)) {
  AllocChecker ac;
  auto *database = new (ac) dbm::Database;
  if (!ac) {
    libc_errno = ENOMEM;
    return nullptr;
  }
  auto opened = database->open(path, flags, mode);
  if (!opened) {
    int error = opened.error();
    delete database;
    libc_errno = error;
    return nullptr;
  }
  return reinterpret_cast<DBM *>(database);
}
} // namespace LIBC_NAMESPACE_DECL
