//===-- MMIX Linux signal UAPI checks -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <setjmp.h>
#include <signal.h>
#include <ucontext.h>

#include <asm/rstack.h>
#include <asm/sigcontext.h>
#include <asm/unistd.h>

#if !defined(__mmix__) || !defined(__linux__)
#error "MMIX signal preparation requires Linux"
#endif
#if !defined(__NR_mmix_rstack_sync) || !defined(__NR_mmix_rstack_query) ||     \
    !defined(__NR_mmix_rstack_jump) || !defined(__NR_rt_sigreturn)
#error "MMIX signal preparation requires register-stack and signal-return UAPI"
#endif

// Compile-only composition checks, not a provider or proof of kernel behavior.
static_assert(sizeof(long) == 8 && sizeof(void *) == 8);
static_assert(sizeof(sigset_t) == 8 && sizeof(siginfo_t) == 128);
static_assert(sizeof(stack_t) == 24 && NSIG == 65);
static_assert(sizeof(jmp_buf) == 304 && sizeof(sigjmp_buf) == 304);
static_assert(sizeof(mcontext_t) == sizeof(sigcontext));
static_assert(alignof(mcontext_t) == alignof(sigcontext));
#define CHECK_CONTEXT_FIELD(field)                                             \
  static_assert(__builtin_offsetof(mcontext_t, field) ==                       \
                __builtin_offsetof(sigcontext, field))
CHECK_CONTEXT_FIELD(sc_regs);
CHECK_CONTEXT_FIELD(sc_regs.regs);
CHECK_CONTEXT_FIELD(sc_regs.pc);
CHECK_CONTEXT_FIELD(sc_regs.r_g);
CHECK_CONTEXT_FIELD(sc_regs.r_l);
CHECK_CONTEXT_FIELD(sc_regs.r_o);
CHECK_CONTEXT_FIELD(sc_regs.r_z);
CHECK_CONTEXT_FIELD(sc_rstack);
CHECK_CONTEXT_FIELD(sc_rstack.start);
CHECK_CONTEXT_FIELD(sc_rstack.count);
CHECK_CONTEXT_FIELD(sc_rstack.data);
CHECK_CONTEXT_FIELD(reserved);
#undef CHECK_CONTEXT_FIELD
static_assert(sizeof(ucontext_t) == 10624 && alignof(ucontext_t) == 8);
static_assert(__builtin_offsetof(ucontext_t, uc_sigmask) == 40);
static_assert(__builtin_offsetof(ucontext_t, uc_mcontext) == 168);
static_assert(sizeof(siginfo_t) + sizeof(ucontext_t) == 10752);
static_assert(sizeof(mmix_rstack_query) == 16);
static_assert(sizeof(mmix_rstack_jump) == 48);
static_assert(MMIX_RSTACK_JUMP_SETMASK == 1);
