// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/netinet/ether_aton.h"
#include "src/__support/common.h"
#include "src/netinet/ether_aton_r.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct ether_addr *, ether_aton, (const char *text)) {
  static struct ether_addr address;
  return LIBC_NAMESPACE::ether_aton_r(text, &address);
}
} // namespace LIBC_NAMESPACE_DECL
