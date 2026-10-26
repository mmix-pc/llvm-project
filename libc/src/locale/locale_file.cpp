//===-- Controlled locale file names --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale_file.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

const char *locale_file_name(cpp::string_view name) {
  constexpr const char *NAMES[] = {"en_US.UTF-8", "de_DE.UTF-8", "tr_TR.UTF-8",
                                   STATEFUL_VALIDATION_LOCALE};
  for (const char *candidate : NAMES)
    if (name == candidate)
      return candidate;
  return nullptr;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
