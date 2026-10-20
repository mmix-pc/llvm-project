//===-- Implementation of getgrent ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/getgrent.h"
#include "src/__support/common.h"
#include "src/grp/stream.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct group *, getgrent, ()) {
  auto file = group_db::enumeration_stream();
  if (!file) {
    libc_errno = file.error();
    return nullptr;
  }
  return group_db::read_shared(file.value());
}
} // namespace LIBC_NAMESPACE_DECL
