//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SHADOW_LOOKUP_H
#define LLVM_LIBC_SRC_SHADOW_LOOKUP_H

#include "hdr/types/struct_spwd.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/account_database.h"

namespace LIBC_NAMESPACE_DECL {
namespace shadow_db {
const char *lookup_path();
// Tests must set the path before starting readers and reset it after joining.
void TESTONLY_set_lookup_path(const char *path);
extern internal::AccountBuffer<struct spwd> lookup_buffer;
int parse_shadow(char *buffer, size_t length, size_t size, struct spwd *entry);

template <typename Match>
int lookup_shadow(Match match, struct spwd *entry, char *buffer, size_t size,
                  struct spwd **result, const char *path = lookup_path()) {
  return internal::lookup_account(path, entry, buffer, size, result,
                                  parse_shadow, match);
}
} // namespace shadow_db
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_SHADOW_LOOKUP_H
