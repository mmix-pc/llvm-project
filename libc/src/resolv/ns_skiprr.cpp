//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/resolv/ns_skiprr.h"
#include "src/__support/common.h"
#include "src/resolv/dns_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ns_skiprr,
                   (const unsigned char *src, const unsigned char *end,
                    ns_sect section, int count)) {
  if (!dns::valid_section(section) || count < 0)
    return dns::fail(EINVAL);
  if (!src || !end || end < src || end - src > NS_MAXMSG)
    return dns::fail();
  const unsigned char *p = src;
  for (int i = 0; i < count; ++i) {
    int length = dns::skip_name(p, end);
    if (length < 0)
      return -1;
    p += length;
    int tail = dns::record_tail(p, end, section);
    if (tail < 0)
      return -1;
    p += tail;
  }
  return static_cast<int>(p - src);
}

} // namespace LIBC_NAMESPACE_DECL
