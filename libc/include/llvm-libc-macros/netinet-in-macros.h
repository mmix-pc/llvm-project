//===-- Definition of macros from netinet/in.h ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_NETINET_IN_MACROS_H
#define LLVM_LIBC_MACROS_NETINET_IN_MACROS_H

#include "../__llvm-libc-common.h"
#include "../llvm-libc-types/in_addr_t.h"
#include "../llvm-libc-types/struct_in6_addr.h"

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define IPPROTO_RAW 255
#define IPPROTO_HOPOPTS 0
#define IPPROTO_IGMP 2
#define IPPROTO_IPIP 4
#define IPPROTO_ROUTING 43
#define IPPROTO_FRAGMENT 44
#define IPPROTO_GRE 47
#define IPPROTO_ESP 50
#define IPPROTO_AH 51
#define IPPROTO_ICMPV6 58
#define IPPROTO_NONE 59
#define IPPROTO_DSTOPTS 60
#define IN_CLASSD(address) (((__UINT32_TYPE__)(address) & 0xf0000000U) == 0xe0000000U)
#define IN_MULTICAST(address) IN_CLASSD(address)

#define INADDR_ANY __LLVM_LIBC_CAST(static_cast, in_addr_t, 0x00000000)
#define INADDR_BROADCAST __LLVM_LIBC_CAST(static_cast, in_addr_t, 0xffffffff)
#define INADDR_NONE __LLVM_LIBC_CAST(static_cast, in_addr_t, 0xffffffff)
// Not specified by POSIX, added in SVR4
#define INADDR_LOOPBACK __LLVM_LIBC_CAST(static_cast, in_addr_t, 0x7f000001)

#define IN6ADDR_ANY_INIT                                                       \
  {                                                                            \
    {                                                                          \
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }                       \
    }                                                                          \
  }
#define IN6ADDR_LOOPBACK_INIT                                                  \
  {                                                                            \
    {                                                                          \
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }                       \
    }                                                                          \
  }

// The following macros test for special IPv6 addresses. Each macro is of type
// int and takes a single argument of type const struct in6_addr *:
// https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/netinet_in.h.html

// Ordinary inline calls also work in file-scope unevaluated expressions and
// C++ initializers. Keep pointer arguments single-evaluated in every macro.
static inline int
__llvm_libc_in6_unspecified(const struct in6_addr *__address) {
  for (unsigned __i = 0; __i < 16; ++__i)
    if (__address->s6_addr[__i] != 0)
      return 0;
  return 1;
}

static inline int __llvm_libc_in6_loopback(const struct in6_addr *__address) {
  for (unsigned __i = 0; __i < 15; ++__i)
    if (__address->s6_addr[__i] != 0)
      return 0;
  return __address->s6_addr[15] == 1;
}

static inline int __llvm_libc_in6_multicast(const struct in6_addr *__address) {
  return __address->s6_addr[0] == 0xff;
}

static inline int __llvm_libc_in6_linklocal(const struct in6_addr *__address) {
  return __address->s6_addr[0] == 0xfe &&
         (__address->s6_addr[1] & 0xc0) == 0x80;
}

static inline int __llvm_libc_in6_sitelocal(const struct in6_addr *__address) {
  return __address->s6_addr[0] == 0xfe &&
         (__address->s6_addr[1] & 0xc0) == 0xc0;
}

static inline int __llvm_libc_in6_v4mapped(const struct in6_addr *__address) {
  for (unsigned __i = 0; __i < 10; ++__i)
    if (__address->s6_addr[__i] != 0)
      return 0;
  return __address->s6_addr[10] == 0xff && __address->s6_addr[11] == 0xff;
}

static inline int __llvm_libc_in6_v4compat(const struct in6_addr *__address) {
  for (unsigned __i = 0; __i < 12; ++__i)
    if (__address->s6_addr[__i] != 0)
      return 0;
  return !__llvm_libc_in6_unspecified(__address) &&
         !__llvm_libc_in6_loopback(__address);
}

static inline int
__llvm_libc_in6_multicast_scope(const struct in6_addr *__address,
                                unsigned __scope) {
  return __llvm_libc_in6_multicast(__address) &&
         (__address->s6_addr[1] & 0xf) == __scope;
}

#define IN6_IS_ADDR_UNSPECIFIED(a) (__llvm_libc_in6_unspecified((a)))
#define IN6_IS_ADDR_LOOPBACK(a) (__llvm_libc_in6_loopback((a)))
#define IN6_IS_ADDR_MULTICAST(a) (__llvm_libc_in6_multicast((a)))
#define IN6_IS_ADDR_LINKLOCAL(a) (__llvm_libc_in6_linklocal((a)))
#define IN6_IS_ADDR_SITELOCAL(a) (__llvm_libc_in6_sitelocal((a)))
#define IN6_IS_ADDR_V4MAPPED(a) (__llvm_libc_in6_v4mapped((a)))
#define IN6_IS_ADDR_V4COMPAT(a) (__llvm_libc_in6_v4compat((a)))
#define IN6_IS_ADDR_MC_NODELOCAL(a) (__llvm_libc_in6_multicast_scope((a), 0x1))
#define IN6_IS_ADDR_MC_LINKLOCAL(a) (__llvm_libc_in6_multicast_scope((a), 0x2))
#define IN6_IS_ADDR_MC_SITELOCAL(a) (__llvm_libc_in6_multicast_scope((a), 0x5))
#define IN6_IS_ADDR_MC_ORGLOCAL(a) (__llvm_libc_in6_multicast_scope((a), 0x8))
#define IN6_IS_ADDR_MC_GLOBAL(a) (__llvm_libc_in6_multicast_scope((a), 0xe))

#endif // LLVM_LIBC_MACROS_NETINET_IN_MACROS_H
