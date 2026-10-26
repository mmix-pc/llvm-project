//===-- Implementation of strtof_l ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/strtof_l.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/locale/numeric_conversion.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(float, strtof_l,
                   (const char *__restrict str, char **__restrict str_end,
                    locale_t locale)) {
  auto result = internal::locale_strtofloatingpoint<float>(
      str, internal::numeric_parse_locale(locale));
  if (result.has_error())
    libc_errno = result.error;

  if (str_end != nullptr)
    *str_end = const_cast<char *>(str + result.parsed_len);

  return result.value;
}

} // namespace LIBC_NAMESPACE_DECL
