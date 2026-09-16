//===-- MMIX Linux saved environment bridge ----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_SAVE_H
#define LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_SAVE_H

#include "hdr/types/jmp_buf.h"
#include "src/__support/macros/macro-utils.h"
#include <asm/rstack.h>
#include <asm/unistd.h>

#if !defined(__mmix__) || !defined(__linux__)
#error "The Linux saved environment requires MMIX Linux"
#endif

static_assert(sizeof(__jmp_buf) == 304 && alignof(__jmp_buf) == 8);
static_assert(__builtin_offsetof(__jmp_buf, frame_pointer) == 0);
static_assert(__builtin_offsetof(__jmp_buf, return_address) == 8);
static_assert(__builtin_offsetof(__jmp_buf, stack_pointer) == 16);
static_assert(__builtin_offsetof(__jmp_buf, register_stack_offset) == 24);
static_assert(__builtin_offsetof(__jmp_buf, locals) == 32);
static_assert(__builtin_offsetof(__jmp_buf, chain_id) == 280);
static_assert(__builtin_offsetof(__jmp_buf, signal_mask) == 288);
static_assert(__builtin_offsetof(__jmp_buf, save_mask) == 296);
static_assert(sizeof(mmix_rstack_query) == 16);
static_assert(__builtin_offsetof(mmix_rstack_query, chain_id) == 0);
static_assert(__builtin_offsetof(mmix_rstack_query, sigmask) == 8);

// Direct query preserves r248/r250/r251/r252, rJ and the current window.
// Do not call the C syscall adapter: its extra window would save the wrong O.
// A partially written environment is not valid until this bridge returns.
#define MMIX_LINUX_SAVE_BODY                                                   \
  "OR r251, r231, 0\n"                                                         \
  "GET r252, rJ\n"                                                             \
  "STOU r253, r251, 0\n"                                                       \
  "STOU r252, r251, 8\n"                                                       \
  "STOU r254, r251, 16\n"                                                      \
  "CMPU r248, r232, 0\n"                                                       \
  "SETL r250, 280\n"                                                           \
  "ADDU r250, r251, r250\n"                                                    \
  "SETL r255, 0\n"                                                             \
  "STOU r255, r250, 0\n"                                                       \
  "STOU r255, r250, 16\n"                                                      \
  "OR r231, r250, 0\n"                                                         \
  "SETL r232, 0\n"                                                             \
  "SETL r233, 0\n"                                                             \
  "SETL r234, 0\n"                                                             \
  "SETL r235, 0\n"                                                             \
  "SETL r236, 0\n"                                                             \
  "SETL r237, " LLVM_LIBC_STRINGIFY(                                           \
      __NR_mmix_rstack_query) "\n"                                             \
                              "TRAP 1, 0, 0\n"                                 \
                              "BNZ r231, 9f\n"                                 \
                              "LDOU r255, r250, 0\n"                           \
                              "BZ r255, 9f\n"                                  \
                              "STOU r248, r250, 16\n"                          \
                              "BNZ r248, 1f\n"                                 \
                              "STOU r248, r250, 8\n"                           \
                              "1:\n"                                           \
                              "GETA r255, 2f\n"                                \
                              "PUT rJ, r255\n"                                 \
                              "POP 0, 0\n"                                     \
                              "2:\n"                                           \
                              "GET r255, rO\n"                                 \
                              "STOU r255, r251, 24\n"                          \
                              "ADDU r250, r251, 32\n"                          \
                              "STOU r0, r250, 0\n"                             \
                              "STOU r1, r250, 8\n"                             \
                              "STOU r2, r250, 16\n"                            \
                              "STOU r3, r250, 24\n"                            \
                              "STOU r4, r250, 32\n"                            \
                              "STOU r5, r250, 40\n"                            \
                              "STOU r6, r250, 48\n"                            \
                              "STOU r7, r250, 56\n"                            \
                              "STOU r8, r250, 64\n"                            \
                              "STOU r9, r250, 72\n"                            \
                              "STOU r10, r250, 80\n"                           \
                              "STOU r11, r250, 88\n"                           \
                              "STOU r12, r250, 96\n"                           \
                              "STOU r13, r250, 104\n"                          \
                              "STOU r14, r250, 112\n"                          \
                              "STOU r15, r250, 120\n"                          \
                              "STOU r16, r250, 128\n"                          \
                              "STOU r17, r250, 136\n"                          \
                              "STOU r18, r250, 144\n"                          \
                              "STOU r19, r250, 152\n"                          \
                              "STOU r20, r250, 160\n"                          \
                              "STOU r21, r250, 168\n"                          \
                              "STOU r22, r250, 176\n"                          \
                              "STOU r23, r250, 184\n"                          \
                              "STOU r24, r250, 192\n"                          \
                              "STOU r25, r250, 200\n"                          \
                              "STOU r26, r250, 208\n"                          \
                              "STOU r27, r250, 216\n"                          \
                              "STOU r28, r250, 224\n"                          \
                              "STOU r29, r250, 232\n"                          \
                              "STOU r30, r250, 240\n"                          \
                              "SETL r231, 0\n"                                 \
                              "GO r255, r252, 0\n"                             \
                              "9:\n"                                           \
                              ".hidden __llvm_libc_mmix_jump_fail\n"           \
                              "JMP __llvm_libc_mmix_jump_fail\n"

#endif
