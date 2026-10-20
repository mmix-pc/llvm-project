//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_ARPA_NAMESER_MACROS_H
#define LLVM_LIBC_MACROS_ARPA_NAMESER_MACROS_H

#define NS_PACKETSZ 512
#define NS_MAXDNAME 1025
#define NS_MAXMSG 65535
#define NS_MAXCDNAME 255
#define NS_MAXLABEL 63
#define NS_HFIXEDSZ 12
#define NS_QFIXEDSZ 4
#define NS_RRFIXEDSZ 10
#define NS_INT16SZ 2
#define NS_INT32SZ 4
#define NS_CMPRSFLGS 0xc0
#define NS_DEFAULTPORT 53

#define ns_msg_id(handle) ((handle)._id + 0)
#define ns_msg_base(handle) ((handle)._msg + 0)
#define ns_msg_end(handle) ((handle)._eom + 0)
#define ns_msg_size(handle) ((handle)._eom - (handle)._msg)
#define ns_msg_count(handle, section) ((handle)._counts[section] + 0)

#define ns_rr_name(rr) ((rr).name[0] ? (rr).name : ".")
#define ns_rr_type(rr) ((ns_type)((rr).type + 0))
#define ns_rr_class(rr) ((ns_class)((rr).rr_class + 0))
#define ns_rr_ttl(rr) ((rr).ttl + 0)
#define ns_rr_rdlen(rr) ((rr).rdlength + 0)
#define ns_rr_rdata(rr) ((rr).rdata + 0)

// Keep flag access self-contained: no separately linked flag table is needed.
static __inline__ unsigned int __llvm_libc_ns_flag(unsigned int bits,
                                                   int flag) {
  switch (flag) {
  case 0:
    return (bits >> 15) & 1;
  case 1:
    return (bits >> 11) & 15;
  case 2:
    return (bits >> 10) & 1;
  case 3:
    return (bits >> 9) & 1;
  case 4:
    return (bits >> 8) & 1;
  case 5:
    return (bits >> 7) & 1;
  case 6:
    return (bits >> 6) & 1;
  case 7:
    return (bits >> 5) & 1;
  case 8:
    return (bits >> 4) & 1;
  case 9:
    return bits & 15;
  default:
    return 0;
  }
}
#define ns_msg_getflag(handle, flag)                                           \
  __llvm_libc_ns_flag((handle)._flags, (flag))

#define NS_GET16(value, cp)                                                    \
  do {                                                                         \
    const unsigned char *__ns_p = (const unsigned char *)(cp);                 \
    (value) = ((unsigned int)__ns_p[0] << 8) | __ns_p[1];                      \
    (cp) += NS_INT16SZ;                                                        \
  } while (0)
#define NS_GET32(value, cp)                                                    \
  do {                                                                         \
    const unsigned char *__ns_p = (const unsigned char *)(cp);                 \
    (value) = ((unsigned long)__ns_p[0] << 24) |                               \
              ((unsigned long)__ns_p[1] << 16) |                               \
              ((unsigned long)__ns_p[2] << 8) | __ns_p[3];                     \
    (cp) += NS_INT32SZ;                                                        \
  } while (0)

#endif // LLVM_LIBC_MACROS_ARPA_NAMESER_MACROS_H
