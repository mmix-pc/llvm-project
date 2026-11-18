// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_NETINET_IF_ETHER_H
#define LLVM_LIBC_NETINET_IF_ETHER_H
#include <linux/if_ether.h>
#include <net/if_arp.h>
#include "../llvm-libc-types/struct_ether_addr.h"
struct ether_header {
  unsigned char ether_dhost[ETH_ALEN];
  unsigned char ether_shost[ETH_ALEN];
  __UINT16_TYPE__ ether_type;
} __attribute__((packed));
struct ether_arp {
  struct arphdr ea_hdr;
  unsigned char arp_sha[ETH_ALEN];
  unsigned char arp_spa[4];
  unsigned char arp_tha[ETH_ALEN];
  unsigned char arp_tpa[4];
};
#define arp_hrd ea_hdr.ar_hrd
#define arp_pro ea_hdr.ar_pro
#define arp_hln ea_hdr.ar_hln
#define arp_pln ea_hdr.ar_pln
#define arp_op ea_hdr.ar_op
#define ETHERTYPE_IP ETH_P_IP
#define ETHERTYPE_ARP ETH_P_ARP
#define ETHER_ADDR_LEN ETH_ALEN
#define ETHER_TYPE_LEN 2
#define ETHER_CRC_LEN ETH_FCS_LEN
#define ETHER_HDR_LEN ETH_HLEN
#define ETHER_MIN_LEN (ETH_ZLEN + ETH_FCS_LEN)
#define ETHER_MAX_LEN (ETH_FRAME_LEN + ETH_FCS_LEN)
#define ETHERMTU ETH_DATA_LEN
#define ETHERTYPE_IP ETH_P_IP
#define ETHERTYPE_ARP ETH_P_ARP
#define ETHERTYPE_REVARP ETH_P_RARP
#define ETHERTYPE_IPV6 ETH_P_IPV6
#define ETHERTYPE_VLAN ETH_P_8021Q
#endif
