//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_GRP_LOOKUP_H
#define LLVM_LIBC_SRC_GRP_LOOKUP_H
#include "hdr/types/struct_group.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/account_database.h"

namespace LIBC_NAMESPACE_DECL {
namespace group_db {
const char *lookup_path();
void TESTONLY_set_lookup_path(const char *path);
extern internal::AccountBuffer<struct group> lookup_buffer;
int parse_group(char *buffer, size_t length, size_t size, struct group *entry);

template <typename Match>
int lookup_group(Match match, struct group *entry, char *buffer, size_t size,
                 struct group **result, const char *path = lookup_path()) {
  return internal::lookup_account(path, entry, buffer, size, result,
                                  parse_group, match);
}
} // namespace group_db
} // namespace LIBC_NAMESPACE_DECL
#endif
