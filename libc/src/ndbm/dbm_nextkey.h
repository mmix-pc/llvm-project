//===-- Implementation header for dbm_nextkey ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_DBM_NEXTKEY_H
#define LLVM_LIBC_SRC_NDBM_DBM_NEXTKEY_H

#include "hdr/types/DBM.h"
#include "hdr/types/datum.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
datum dbm_nextkey(DBM *db);
} // namespace LIBC_NAMESPACE_DECL
#endif
