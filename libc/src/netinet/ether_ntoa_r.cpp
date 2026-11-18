// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/netinet/ether_ntoa_r.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, ether_ntoa_r,
                   (const struct ether_addr *address, char *text)) {
  constexpr char hex[] = "0123456789abcdef";
  char *out = text;
  for (unsigned i = 0; i < 6; ++i) {
    unsigned byte = address->ether_addr_octet[i];
    if (i)
      *out++ = ':';
    if (byte >= 16)
      *out++ = hex[byte >> 4];
    *out++ = hex[byte & 15];
  }
  *out = '\0';
  return text;
}
} // namespace LIBC_NAMESPACE_DECL
