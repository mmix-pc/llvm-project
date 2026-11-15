//===-- MMIX Linux resolver error descriptions ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/gai_strerror.h"
#include "hdr/netdb_macros.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(const char *, gai_strerror, (int error)) {
  switch (error) {
  case 0:
    return "Success";
  case EAI_AGAIN:
    return "Temporary name resolution failure";
  case EAI_BADFLAGS:
    return "Invalid address information flags";
  case EAI_FAIL:
    return "Nonrecoverable name resolution failure";
  case EAI_FAMILY:
    return "Unsupported address family";
  case EAI_MEMORY:
    return "Memory allocation failure";
  case EAI_NONAME:
    return "Name or service not found";
  case EAI_SERVICE:
    return "Service unavailable for socket type";
  case EAI_SOCKTYPE:
    return "Unsupported socket type";
  case EAI_SYSTEM:
    return "System error";
  case EAI_OVERFLOW:
    return "Output buffer too small";
  default:
    return "Unknown address information error";
  }
}
} // namespace LIBC_NAMESPACE_DECL
