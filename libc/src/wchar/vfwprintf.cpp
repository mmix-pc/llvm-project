//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/vfwprintf.h"
#include "hdr/errno_macros.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/printf_core/error_mapper.h"
#include "src/__support/printf_core/wide_printf_main.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
struct WideStream {
  File *file;
  int error = 0;
};
int write_wide(void *context, const wchar_t *data, size_t size) {
  auto &stream = *static_cast<WideStream *>(context);
  auto result = stream.file->write_unlocked(data, size);
  if (result.has_error() || result.value != size) {
    stream.error = result.has_error() ? result.error : EIO;
    return printf_core::FILE_WRITE_ERROR;
  }
  return printf_core::WRITE_OK;
}
} // namespace
LLVM_LIBC_FUNCTION(int, vfwprintf,
                   (::FILE *__restrict stream, const wchar_t *__restrict format,
                    va_list vlist)) {
  WideStream output{reinterpret_cast<File *>(stream)};
  File::FileLock lock(output.file);
  if (write_wide(&output, L"", 0) != printf_core::WRITE_OK) {
    libc_errno = output.error;
    return -1;
  }
  internal::ArgList args(vlist);
  printf_core::WideWriter writer({&output, write_wide});
  auto result = printf_core::wide_printf_main(&writer, format, args);
  if (!result.has_value()) {
    libc_errno = output.error
                     ? output.error
                     : printf_core::internal_error_to_errno(result.error());
    return -1;
  }
  return static_cast<int>(result.value());
}
} // namespace LIBC_NAMESPACE_DECL
