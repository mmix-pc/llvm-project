//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_NS_FLAG_H
#define LLVM_LIBC_TYPES_NS_FLAG_H

typedef enum __ns_flag {
  ns_f_qr = 0,
  ns_f_opcode = 1,
  ns_f_aa = 2,
  ns_f_tc = 3,
  ns_f_rd = 4,
  ns_f_ra = 5,
  ns_f_z = 6,
  ns_f_ad = 7,
  ns_f_cd = 8,
  ns_f_rcode = 9,
  ns_f_max = 10
} ns_flag;

#endif // LLVM_LIBC_TYPES_NS_FLAG_H
