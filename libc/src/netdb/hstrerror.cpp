// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/netdb/hstrerror.h"
#include "hdr/netdb_macros.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(const char *, hstrerror, (int error)) {
  if (error < 0)
    return "Resolver internal error";
  switch (error) {
  case 0: return "Resolver Error 0 (no error)";
  case HOST_NOT_FOUND: return "Unknown host";
  case TRY_AGAIN: return "Host name lookup failure";
  case NO_RECOVERY: return "Unknown server error";
  case NO_DATA: return "No address associated with name";
  default: return "Unknown resolver error";
  }
}
} // namespace LIBC_NAMESPACE_DECL
