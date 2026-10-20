//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/resolv/dn_expand.h"
#include "src/__support/common.h"
#include "src/resolv/dns_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, dn_expand,
                   (const unsigned char *msg, const unsigned char *end,
                    const unsigned char *src, char *dst, int size)) {
  if (!dst || size <= 0)
    return dns::fail();
  char name[NS_MAXDNAME];
  int consumed = dns::expand_name(msg, end, src, name, sizeof(name));
  if (consumed < 0)
    return -1;
  if (name[0] == '.' && name[1] == 0)
    name[0] = 0;
  int length = 0;
  while (name[length])
    ++length;
  if (length >= size)
    return dns::fail();
  for (int i = 0; i <= length; ++i)
    dst[i] = name[i];
  return consumed;
}

} // namespace LIBC_NAMESPACE_DECL
