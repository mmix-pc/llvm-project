//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of the swprintf function.
///
//===----------------------------------------------------------------------===//

#include "src/wchar/swprintf.h"

#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/__support/printf_core/error_mapper.h"
#include "src/__support/printf_core/wide_printf_main.h"

#include <stdarg.h>

#ifdef LIBC_COPT_PRINTF_DISABLE_WIDE
#error "swprintf requires wide character conversion support"
#endif

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, swprintf,
                   (wchar_t *__restrict buffer, size_t bufsz,
                    const wchar_t *__restrict format, ...)) {
  va_list vlist;
  va_start(vlist, format);
  internal::ArgList args(vlist);
  va_end(vlist);
  printf_core::WideWriter writer(buffer, bufsz);
  auto result = printf_core::wide_printf_main(&writer, format, args);
  if (!result.has_value()) {
    // Unlike snprintf, truncation fails without a would-have-written count.
    if (result.error() != -printf_core::BUFFER_TOO_SMALL)
      libc_errno = printf_core::internal_error_to_errno(result.error());
    return -1;
  }
  // WideWriter checks INT_MAX before each write.
  return static_cast<int>(result.value());
}

} // namespace LIBC_NAMESPACE_DECL
