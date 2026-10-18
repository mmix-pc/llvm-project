//===-- Memory stream positioning ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_FILE_MEMORY_FILE_H
#define LLVM_LIBC_SRC___SUPPORT_FILE_MEMORY_FILE_H

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/off_t.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/error_or.h"
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {
// Leave room for the terminator and keep pointer differences representable.
inline constexpr size_t MEMORY_FILE_MAX =
    cpp::numeric_limits<off_t>::max() <
            cpp::numeric_limits<ptrdiff_t>::max() - 1
        ? cpp::numeric_limits<off_t>::max()
        : cpp::numeric_limits<ptrdiff_t>::max() - 1;

inline ErrorOr<off_t> memory_file_seek(size_t position, size_t length,
                                       size_t limit, off_t offset, int whence) {
  off_t base;
  switch (whence) {
  case SEEK_SET:
    base = 0;
    break;
  case SEEK_CUR:
    base = static_cast<off_t>(position);
    break;
  case SEEK_END:
    base = static_cast<off_t>(length);
    break;
  default:
    return Error(EINVAL);
  }
  off_t result;
  if (__builtin_add_overflow(base, offset, &result) || result < 0 ||
      static_cast<size_t>(result) > limit)
    return Error(EINVAL);
  return result;
}
} // namespace LIBC_NAMESPACE_DECL

#endif
