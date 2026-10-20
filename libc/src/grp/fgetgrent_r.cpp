//===-- Implementation of fgetgrent_r -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/fgetgrent_r.h"
#include "src/__support/common.h"
#include "src/grp/stream.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, fgetgrent_r,
                   (::FILE * stream, struct group *entry, char *buffer,
                    size_t size, struct group **result)) {
  return group_db::read_entry(reinterpret_cast<File *>(stream), entry, buffer,
                              size, result);
}
} // namespace LIBC_NAMESPACE_DECL
