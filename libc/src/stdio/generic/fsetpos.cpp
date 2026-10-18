//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/fsetpos.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, fsetpos, (::FILE * stream, const fpos_t *position)) {
  File::Position saved{position->__offset,
                       {position->__state.__field1, position->__state.__field2,
                        position->__state.__field3}};
  auto result = reinterpret_cast<File *>(stream)->set_position(saved);
  if (!result.has_value()) {
    libc_errno = result.error();
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
