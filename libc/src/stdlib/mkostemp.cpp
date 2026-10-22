//===-- Implementation of mkostemp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/mkostemp.h"
#include "src/stdlib/mkstemp_impl.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mkostemp, (char *tmpl, int flags)) {
  // All accepted flags reach the exclusive open atomically. In particular,
  // never emulate close-on-exec with a later descriptor update.
  constexpr int allowed = O_APPEND | O_CLOEXEC | O_DSYNC | O_SYNC | O_RSYNC;
  if (flags & ~allowed) {
    libc_errno = EINVAL;
    return -1;
  }
  return internal::create_temporary_file(tmpl, flags);
}

} // namespace LIBC_NAMESPACE_DECL
