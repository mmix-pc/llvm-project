//===-- Implementation of catclose ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/nl_types/catclose.h"
#include "include/llvm-libc-types/nl_catd.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/nl_types/catalog_data.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, catclose, (nl_catd catalog)) {
  if (!catalog || catalog == reinterpret_cast<nl_catd>(-1)) {
    libc_errno = EBADF;
    return -1;
  }
  static_cast<internal::MessageCatalog *>(catalog)->destroy();
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
