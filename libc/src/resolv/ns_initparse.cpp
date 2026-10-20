//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/resolv/ns_initparse.h"
#include "src/__support/common.h"
#include "src/resolv/dns_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ns_initparse,
                   (const unsigned char *msg, int size, ns_msg *handle)) {
  if (!handle)
    return dns::fail(EINVAL);
  if (!msg || size < NS_HFIXEDSZ || size > NS_MAXMSG)
    return dns::fail();
  ns_msg result = {};
  result._msg = msg;
  result._eom = msg + size;
  result._id = dns::get16(msg);
  result._flags = dns::get16(msg + 2);
  const unsigned char *p = msg + NS_HFIXEDSZ;
  for (int s = 0; s < ns_s_max; ++s) {
    result._counts[s] = dns::get16(msg + 4 + s * 2);
    result._sections[s] = result._counts[s] ? p : nullptr;
    for (unsigned int i = 0; i < result._counts[s]; ++i) {
      int length = dns::expand_name(msg, result._eom, p, nullptr, 0);
      if (length < 0)
        return -1;
      p += length;
      int tail = dns::record_tail(p, result._eom, static_cast<ns_sect>(s));
      if (tail < 0)
        return -1;
      p += tail;
    }
  }
  if (p != result._eom)
    return dns::fail();
  result._sect = ns_s_max;
  result._rrnum = -1;
  *handle = result;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
