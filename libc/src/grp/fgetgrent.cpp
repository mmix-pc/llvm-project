//===-- Implementation of fgetgrent ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/fgetgrent.h"
#include "src/__support/common.h"
#include "src/grp/stream.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct group *, fgetgrent, (::FILE * stream)) {
  return group_db::read_shared(reinterpret_cast<File *>(stream));
}
} // namespace LIBC_NAMESPACE_DECL
