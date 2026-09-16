//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifdef REVERSE_INCLUDES
#include "../../include/llvm-libc-types/ucontext_t.h"
#include "../../include/llvm-libc-macros/signal-macros.h"
#else
#include "../../include/llvm-libc-macros/signal-macros.h"
#include "../../include/llvm-libc-types/ucontext_t.h"
#endif
#include "../../include/llvm-libc-types/siginfo_t.h"
#include "../../include/llvm-libc-types/mcontext_t.h"
#include "../../include/llvm-libc-types/ucontext_t.h"

#ifdef __cplusplus
#define CHECK static_assert
#define ALIGNOF alignof
#else
#define CHECK _Static_assert
#define ALIGNOF _Alignof
#endif
#define OFFSET(T, F, N) CHECK(__builtin_offsetof(T, F) == N, #F)

CHECK(MINSIGSTKSZ == 32768, "minimum signal stack");
CHECK(SIGSTKSZ == 65536, "default signal stack");
CHECK(SS_AUTODISARM == 0x80000000U, "autodisarm");
CHECK(sizeof(siginfo_t) == 128, "siginfo");
CHECK(sizeof(sigset_t) == 8, "mask");
CHECK(sizeof(stack_t) == 24, "stack");
CHECK(ALIGNOF(stack_t) == 8, "stack alignment");
OFFSET(stack_t, ss_sp, 0);
OFFSET(stack_t, ss_flags, 8);
OFFSET(stack_t, ss_size, 16);
CHECK(sizeof(mcontext_t) == 10456, "machine context");
CHECK(ALIGNOF(mcontext_t) == 8, "machine alignment");
OFFSET(mcontext_t, sc_regs, 0);
OFFSET(mcontext_t, sc_regs.regs, 0);
OFFSET(mcontext_t, sc_regs.pc, 2048);
OFFSET(mcontext_t, sc_regs.r_g, 2056);
OFFSET(mcontext_t, sc_regs.r_l, 2064);
OFFSET(mcontext_t, sc_regs.r_o, 2072);
OFFSET(mcontext_t, sc_regs.r_a, 2080);
OFFSET(mcontext_t, sc_regs.r_b, 2088);
OFFSET(mcontext_t, sc_regs.r_d, 2096);
OFFSET(mcontext_t, sc_regs.r_e, 2104);
OFFSET(mcontext_t, sc_regs.r_h, 2112);
OFFSET(mcontext_t, sc_regs.r_j, 2120);
OFFSET(mcontext_t, sc_regs.r_m, 2128);
OFFSET(mcontext_t, sc_regs.r_p, 2136);
OFFSET(mcontext_t, sc_regs.r_r, 2144);
OFFSET(mcontext_t, sc_regs.r_w, 2152);
OFFSET(mcontext_t, sc_regs.r_x, 2160);
OFFSET(mcontext_t, sc_regs.r_y, 2168);
OFFSET(mcontext_t, sc_regs.r_z, 2176);
OFFSET(mcontext_t, sc_rstack, 2184);
OFFSET(mcontext_t, sc_rstack.start, 2184);
OFFSET(mcontext_t, sc_rstack.count, 2192);
OFFSET(mcontext_t, sc_rstack.data, 2200);
OFFSET(mcontext_t, reserved, 10392);
CHECK(sizeof(ucontext_t) == 10624, "user context");
CHECK(ALIGNOF(ucontext_t) == 8, "user alignment");
OFFSET(ucontext_t, uc_flags, 0);
OFFSET(ucontext_t, uc_link, 8);
OFFSET(ucontext_t, uc_stack, 16);
OFFSET(ucontext_t, uc_sigmask, 40);
OFFSET(ucontext_t, __unused, 48);
OFFSET(ucontext_t, uc_mcontext, 168);
