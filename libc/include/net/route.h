// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_NET_ROUTE_H
#define LLVM_LIBC_NET_ROUTE_H
#include <net/if.h>
#include <netinet/in.h>
#include <linux/route.h>
struct in6_rtmsg {
  struct in6_addr rtmsg_dst, rtmsg_src, rtmsg_gateway;
  uint32_t rtmsg_type;
  uint16_t rtmsg_dst_len, rtmsg_src_len;
  uint32_t rtmsg_metric;
  unsigned long rtmsg_info;
  uint32_t rtmsg_flags;
  int rtmsg_ifindex;
};
#define RTF_DEFAULT 0x00010000
#define RTF_ALLONLINK 0x00020000
#define RTF_ADDRCONF 0x00040000
#define RTF_PREFIX_RT 0x00080000
#define RTF_ANYCAST 0x00100000
#define RTF_NONEXTHOP 0x00200000
#define RTF_EXPIRES 0x00400000
#define RTF_ROUTEINFO 0x00800000
#define RTF_CACHE 0x01000000
#define RTF_FLOW 0x02000000
#define RTF_POLICY 0x04000000
#define RTF_LOCAL 0x80000000
#endif
