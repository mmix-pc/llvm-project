//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/vfwscanf.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/stdio/scanf_core/wide_scanf_main.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
class StreamSource {
  File *file;

public:
  explicit StreamSource(File *file) : file(file) {}
  wint_t peek() {
    wchar_t c;
    auto result = file->peek_wide_unlocked(&c);
    if (result.has_error())
      libc_errno = result.error;
    return result.value == 1 ? wint_t(c) : WEOF;
  }
  void take() {
    wchar_t c;
    // peek() already established an available character under the stream lock.
    file->read_unlocked(&c, 1);
  }
};
} // namespace

LLVM_LIBC_FUNCTION(int, vfwscanf,
                   (::FILE *__restrict stream, const wchar_t *__restrict format,
                    va_list args)) {
  auto *file = reinterpret_cast<File *>(stream);
  File::FileLock lock(file);
  wchar_t unused;
  auto oriented = file->read_unlocked(&unused, 0);
  if (oriented.has_error()) {
    libc_errno = oriented.error;
    return EOF;
  }
  internal::ArgList list(args);
  StreamSource source(file);
  int status = scanf_core::READ_OK;
  int result = scanf_core::wide_scanf_main(source, format, list, &status);
  if (status == scanf_core::ALLOCATION_FAILURE ||
      status == scanf_core::ENCODING_FAILURE)
    file->set_error_unlocked();
  return result;
}
} // namespace LIBC_NAMESPACE_DECL
