//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_PWD_LOOKUP_H
#define LLVM_LIBC_SRC_PWD_LOOKUP_H
#include "src/__support/File/account_database.h"
#include "src/pwd/pwd_utils.h"

namespace LIBC_NAMESPACE_DECL {
namespace pwd {
const char *lookup_path();
void TESTONLY_set_lookup_path(const char *path);
extern internal::AccountBuffer<struct passwd> lookup_buffer;

template <typename Match>
int lookup_passwd(Match match, struct passwd *entry, char *buffer, size_t size,
                  struct passwd **result, const char *path = lookup_path()) {
  return internal::lookup_account(
      path, entry, buffer, size, result,
      [](char *line, size_t length, size_t, struct passwd *candidate) {
        return parse_line(cpp::span<char>(line, length), candidate) ? 0
                                                                    : EINVAL;
      },
      match);
}
} // namespace pwd
} // namespace LIBC_NAMESPACE_DECL
#endif
