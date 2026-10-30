//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/grp/endgrent.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/grp/stream.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, endgrent, ()) {
  int saved_errno = libc_errno;
  int error = group_db::close_enumeration();
  libc_errno = error ? error : saved_errno;
}
} // namespace LIBC_NAMESPACE_DECL
