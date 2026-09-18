//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/grp/endgrent.h"
#include "hdr/func/free.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/grp/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, endgrent, ()) {
  // Queries own and close their streams; only the shared result cache persists.
  int saved_errno = libc_errno;
  ::free(group_db::lookup_buffer.buffer);
  group_db::lookup_buffer = {};
  libc_errno = saved_errno;
}
} // namespace LIBC_NAMESPACE_DECL
