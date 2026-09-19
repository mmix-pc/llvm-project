//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of freeaddrinfo.
///
//===----------------------------------------------------------------------===//

#include "src/netdb/freeaddrinfo.h"
#include "hdr/func/free.h"
#include "hdr/types/struct_addrinfo.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, freeaddrinfo, (struct addrinfo * res)) {
  // Each node owns its embedded socket address and optional canonical name.
  while (res) {
    addrinfo *next = res->ai_next;
    ::free(res);
    res = next;
  }
}

} // namespace LIBC_NAMESPACE_DECL
