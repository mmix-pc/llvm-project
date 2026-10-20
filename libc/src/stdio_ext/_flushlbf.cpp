//===-- Flush line-buffered output streams
//---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio_ext/_flushlbf.h"
#include "src/__support/CPP/scope.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/stderr.h"
#include "src/stdio/stdin.h"
#include "src/stdio/stdout.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, _flushlbf, ()) {
  auto flush = [](File *file) {
    if (!file)
      return;
    File::FileLock lock(file);
    if (file->line_buffered() && file->writing()) {
      int error = file->flush_unlocked();
      if (error)
        libc_errno = error;
    }
  };

  File *standard[] = {reinterpret_cast<File *>(stdin),
                      reinterpret_cast<File *>(stdout),
                      reinterpret_cast<File *>(stderr)};
  File::lock_list();
  cpp::scope_exit unlock([] { File::unlock_list(); });
  for (File *file = File::get_first_file(); file; file = file->get_next())
    flush(file);
  for (size_t i = 0; i < 3; ++i) {
    bool seen = false;
    for (size_t j = 0; j < i; ++j)
      seen |= standard[j] == standard[i];
    for (File *file = File::get_first_file(); file; file = file->get_next())
      seen |= file == standard[i];
    if (!seen)
      flush(standard[i]);
  }
}

} // namespace LIBC_NAMESPACE_DECL
