//===-- Implementation of getgrent_r --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/getgrent_r.h"
#include "src/__support/common.h"
#include "src/grp/stream.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getgrent_r,
                   (struct group * entry, char *buffer, size_t size,
                    struct group **result)) {
  if (!result)
    return EINVAL;
  *result = nullptr;
  int saved = libc_errno;
  auto file = group_db::enumeration_stream();
  libc_errno = saved;
  if (!file)
    return file.error();
  return group_db::read_entry(file.value(), entry, buffer, size, result);
}
} // namespace LIBC_NAMESPACE_DECL
