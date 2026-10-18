//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/fgetpos.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, fgetpos,
                   (::FILE *__restrict stream, fpos_t *__restrict position)) {
  auto result = reinterpret_cast<File *>(stream)->get_position();
  if (!result.has_value()) {
    libc_errno = result.error();
    return -1;
  }
  const auto &saved = result.value();
  position->__offset = saved.offset;
  position->__state = {saved.state.partial, saved.state.bytes_stored,
                       saved.state.total_bytes};
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
