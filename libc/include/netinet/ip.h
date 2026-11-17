// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_NETINET_IP_H
#define LLVM_LIBC_NETINET_IP_H
#include <netinet/in.h>
#include <linux/ip.h>
#define IPVERSION 4
#define IP_MAXPACKET 65535
#define MAXTTL 255
#define IPDEFTTL 64
#define MAX_IPOPTLEN 40
#define IP_RF 0x8000
#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff
#define IPTOS_DSCP_MASK 0xfc
#define IPTOS_DSCP(tos) ((tos) & IPTOS_DSCP_MASK)
#define IPTOS_DSCP_AF11 0x28
#define IPTOS_DSCP_AF12 0x30
#define IPTOS_DSCP_AF13 0x38
#define IPTOS_DSCP_AF21 0x48
#define IPTOS_DSCP_AF22 0x50
#define IPTOS_DSCP_AF23 0x58
#define IPTOS_DSCP_AF31 0x68
#define IPTOS_DSCP_AF32 0x70
#define IPTOS_DSCP_AF33 0x78
#define IPTOS_DSCP_AF41 0x88
#define IPTOS_DSCP_AF42 0x90
#define IPTOS_DSCP_AF43 0x98
#define IPTOS_DSCP_EF 0xb8
#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_DEFAULT)
struct ip {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  unsigned int ip_hl : 4, ip_v : 4;
#else
  unsigned int ip_v : 4, ip_hl : 4;
#endif
  uint8_t ip_tos;
  uint16_t ip_len, ip_id, ip_off;
  uint8_t ip_ttl, ip_p;
  uint16_t ip_sum;
  struct in_addr ip_src, ip_dst;
};
#endif
#endif
