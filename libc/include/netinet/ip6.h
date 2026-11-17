// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_NETINET_IP6_H
#define LLVM_LIBC_NETINET_IP6_H
#include <netinet/in.h>
#include <stdint.h>
struct ip6_hdr {
  union {
    struct {
      uint32_t ip6_un1_flow;
      uint16_t ip6_un1_plen;
      uint8_t ip6_un1_nxt, ip6_un1_hlim;
    } ip6_un1;
    uint8_t ip6_un2_vfc;
  } ip6_ctlun;
  struct in6_addr ip6_src, ip6_dst;
};
#define ip6_vfc ip6_ctlun.ip6_un2_vfc
#define ip6_flow ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_plen ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim ip6_ctlun.ip6_un1.ip6_un1_hlim
#define ip6_hops ip6_hlim
struct ip6_ext { uint8_t ip6e_nxt, ip6e_len; };
struct ip6_hbh { uint8_t ip6h_nxt, ip6h_len; };
struct ip6_dest { uint8_t ip6d_nxt, ip6d_len; };
struct ip6_rthdr {
  uint8_t ip6r_nxt, ip6r_len, ip6r_type, ip6r_segleft;
};
struct ip6_frag {
  uint8_t ip6f_nxt, ip6f_reserved;
  uint16_t ip6f_offlg;
  uint32_t ip6f_ident;
};
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define IP6F_OFF_MASK 0xf8ff
#define IP6F_RESERVED_MASK 0x0600
#define IP6F_MORE_FRAG 0x0100
#else
#define IP6F_OFF_MASK 0xfff8
#define IP6F_RESERVED_MASK 0x0006
#define IP6F_MORE_FRAG 0x0001
#endif
#endif
