//===-- Implementation of catgets -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/nl_types/catgets.h"
#include "include/llvm-libc-types/nl_catd.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/nl_types/catalog_data.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, catgets,
                   (nl_catd catalog, int set_number,
                    int message_number, const char *message)) {
  if (!catalog || catalog == reinterpret_cast<nl_catd>(-1)) {
    libc_errno = EBADF;
    return const_cast<char *>(message);
  }
  const char *found = static_cast<internal::MessageCatalog *>(catalog)->lookup(
      set_number, message_number);
  if (found)
    return const_cast<char *>(found);
  libc_errno = ENOMSG;
  return const_cast<char *>(message);
}

} // namespace LIBC_NAMESPACE_DECL
