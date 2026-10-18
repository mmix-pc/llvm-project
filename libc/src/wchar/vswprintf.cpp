//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/vswprintf.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/printf_core/error_mapper.h"
#include "src/__support/printf_core/wide_printf_main.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, vswprintf,
                   (wchar_t *__restrict buffer, size_t size,
                    const wchar_t *__restrict format, va_list vlist)) {
  internal::ArgList args(vlist);
  printf_core::WideWriter writer(buffer, size);
  auto result = printf_core::wide_printf_main(&writer, format, args);
  if (!result.has_value()) {
    if (result.error() != -printf_core::BUFFER_TOO_SMALL)
      libc_errno = printf_core::internal_error_to_errno(result.error());
    return -1;
  }
  return static_cast<int>(result.value());
}
} // namespace LIBC_NAMESPACE_DECL
