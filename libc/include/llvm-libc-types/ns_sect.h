//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_NS_SECT_H
#define LLVM_LIBC_TYPES_NS_SECT_H

typedef enum __ns_sect {
  ns_s_qd = 0,
  ns_s_zn = 0,
  ns_s_an = 1,
  ns_s_pr = 1,
  ns_s_ns = 2,
  ns_s_ud = 2,
  ns_s_ar = 3,
  ns_s_max = 4
} ns_sect;

#endif // LLVM_LIBC_TYPES_NS_SECT_H
