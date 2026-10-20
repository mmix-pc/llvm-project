//===-- Implementation of setgrent ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/setgrent.h"
#include "src/__support/common.h"
#include "src/grp/stream.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, setgrent, ()) {
  if (int error = group_db::rewind_enumeration())
    libc_errno = error;
}
} // namespace LIBC_NAMESPACE_DECL
