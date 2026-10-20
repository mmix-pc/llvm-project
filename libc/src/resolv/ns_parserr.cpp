//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/resolv/ns_parserr.h"
#include "src/__support/common.h"
#include "src/resolv/dns_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ns_parserr,
                   (ns_msg * handle, ns_sect section, int number, ns_rr *rr)) {
  if (!handle || !rr)
    return dns::fail(EINVAL);
  if (!dns::valid_section(section) || number < -1)
    return dns::fail(ENODEV);
  int target = number;
  if (target == -1)
    target = handle->_sect == section ? handle->_rrnum : 0;
  if (target < 0 || target >= handle->_counts[section])
    return dns::fail(ENODEV);
  const unsigned char *msg = handle->_msg;
  const unsigned char *end = handle->_eom;
  if (!msg || !end || end < msg || end - msg < NS_HFIXEDSZ ||
      end - msg > NS_MAXMSG)
    return dns::fail();
  const unsigned char *p = handle->_sections[section];
  int current = 0;
  if (handle->_sect == section && handle->_rrnum >= 0 &&
      target >= handle->_rrnum) {
    p = handle->_msg_ptr;
    current = handle->_rrnum;
  }
  if (!p || p < msg + NS_HFIXEDSZ || p >= end)
    return dns::fail();

  ns_rr result = {};
  for (;;) {
    int length =
        dns::expand_name(msg, end, p, current == target ? result.name : nullptr,
                         sizeof(result.name));
    if (length < 0)
      return -1;
    p += length;
    int tail = dns::record_tail(p, end, section);
    if (tail < 0)
      return -1;
    if (current == target) {
      result.type = dns::get16(p);
      result.rr_class = dns::get16(p + 2);
      if (section != ns_s_qd) {
        result.ttl = dns::get32(p + 4);
        result.rdlength = dns::get16(p + 8);
        result.rdata = p + NS_RRFIXEDSZ;
      }
      p += tail;
      break;
    }
    p += tail;
    ++current;
  }
  *rr = result;
  handle->_sect = section;
  handle->_rrnum = target + 1;
  handle->_msg_ptr = p;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
