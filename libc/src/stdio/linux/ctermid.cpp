//===-- Linux controlling terminal pathname -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/ctermid.h"
#include "hdr/stdio_macros.h"
#include "src/__support/common.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, ctermid, (char *buffer)) {
  static char name[] = "/dev/tty";
  static_assert(sizeof(name) <= L_ctermid);
  if (!buffer)
    return name;
  inline_memcpy(buffer, name, sizeof(name));
  return buffer;
}

} // namespace LIBC_NAMESPACE_DECL
