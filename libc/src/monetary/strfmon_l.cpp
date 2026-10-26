//===-- Implementation of strfmon_l ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/monetary/strfmon_l.h"
#include "src/__support/libc_errno.h"
#include "src/locale/locale.h"
#include "src/monetary/formatter.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(ssize_t, strfmon_l,
                   (char *__restrict output, size_t capacity, locale_t locale,
                    const char *__restrict format, ...)) {
  va_list list;
  va_start(list, format);
  internal::ArgList args(list);
  va_end(list);
  auto snapshot = internal::locale_snapshot(locale);
  const auto &data = snapshot.data[LC_MONETARY]->monetary;
  auto result = internal::format_money(output, capacity, format, args,
                                       data ? &*data : nullptr);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return ssize_t(*result);
}
} // namespace LIBC_NAMESPACE_DECL
