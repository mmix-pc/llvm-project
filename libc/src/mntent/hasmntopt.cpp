//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/mntent/hasmntopt.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, hasmntopt,
                   (const struct mntent *entry, const char *option)) {
  if (!entry || !entry->mnt_opts || !option || !*option)
    return nullptr;
  cpp::string_view wanted(option);
  char *cursor = entry->mnt_opts;
  while (*cursor) {
    char *start = cursor;
    while (*cursor && *cursor != ',')
      ++cursor;
    cpp::string_view token(start, static_cast<size_t>(cursor - start));
    if (token.starts_with(wanted) &&
        (token.size() == wanted.size() || token[wanted.size()] == '='))
      return start;
    if (*cursor)
      ++cursor;
  }
  return nullptr;
}
} // namespace LIBC_NAMESPACE_DECL
