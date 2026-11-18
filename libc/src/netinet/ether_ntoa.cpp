// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/netinet/ether_ntoa.h"
#include "src/__support/common.h"
#include "src/netinet/ether_ntoa_r.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, ether_ntoa, (const struct ether_addr *address)) {
  static char text[18];
  return LIBC_NAMESPACE::ether_ntoa_r(address, text);
}
} // namespace LIBC_NAMESPACE_DECL
