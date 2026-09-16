//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_MMIX_MCONTEXT_T_H
#define LLVM_LIBC_TYPES_MMIX_MCONTEXT_T_H

// Linux LP64 logical context, not an architectural SAVE image.
typedef struct {
  struct {
    unsigned long regs[256];
    unsigned long pc;
    unsigned long r_g;
    unsigned long r_l;
    unsigned long r_o;
    unsigned long r_a;
    unsigned long r_b;
    unsigned long r_d;
    unsigned long r_e;
    unsigned long r_h;
    unsigned long r_j;
    unsigned long r_m;
    unsigned long r_p;
    unsigned long r_r;
    unsigned long r_w;
    unsigned long r_x;
    unsigned long r_y;
    unsigned long r_z;
  } sc_regs;
  struct {
    unsigned long start;
    unsigned long count;
    unsigned long data[1024];
  } sc_rstack;
  unsigned long reserved[8];
} mcontext_t;

#endif // LLVM_LIBC_TYPES_MMIX_MCONTEXT_T_H
