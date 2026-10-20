//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_NS_TYPE_H
#define LLVM_LIBC_TYPES_NS_TYPE_H

typedef enum __ns_type {
  ns_t_invalid = 0,
  ns_t_a = 1,
  ns_t_ns = 2,
  ns_t_md = 3,
  ns_t_mf = 4,
  ns_t_cname = 5,
  ns_t_soa = 6,
  ns_t_mb = 7,
  ns_t_mg = 8,
  ns_t_mr = 9,
  ns_t_null = 10,
  ns_t_wks = 11,
  ns_t_ptr = 12,
  ns_t_hinfo = 13,
  ns_t_minfo = 14,
  ns_t_mx = 15,
  ns_t_txt = 16,
  ns_t_aaaa = 28,
  ns_t_srv = 33,
  ns_t_opt = 41,
  ns_t_axfr = 252,
  ns_t_mailb = 253,
  ns_t_maila = 254,
  ns_t_any = 255,
  ns_t_max = 65536
} ns_type;

#endif // LLVM_LIBC_TYPES_NS_TYPE_H
