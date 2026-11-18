// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/netinet/ether_aton_r.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
static int hex(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}
LLVM_LIBC_FUNCTION(struct ether_addr *, ether_aton_r,
                   (const char *text, struct ether_addr *address)) {
  struct ether_addr parsed;
  for (unsigned i = 0; i < 6; ++i) {
    int value = hex(*text);
    if (value < 0)
      return nullptr;
    ++text;
    int digit = hex(*text);
    if (digit >= 0) {
      value = value * 16 + digit;
      ++text;
    }
    parsed.ether_addr_octet[i] = static_cast<unsigned char>(value);
    if (i != 5) {
      if (*text++ != ':')
        return nullptr;
    }
  }
  if (*text && *text != ' ' && (*text < '\t' || *text > '\r'))
    return nullptr;
  *address = parsed;
  return address;
}
} // namespace LIBC_NAMESPACE_DECL
