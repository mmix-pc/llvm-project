//===-- MMIX Linux native thread boundaries -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if !defined(__mmix__) || !defined(__linux__)
#error "The MMIX Linux thread entry requires MMIX Linux"
#endif

#include <sys/syscall.h>

// The five C arguments in r231-r235 already match the Linux clone order.
// The kernel preserves the parent's TP/SP/rJ and logical register stack, but
// gives the child a fresh root, zero rL/rJ/FP and the supplied SP/TP. Never use
// parent locals or a compiler-generated continuation on the zero-result path.
.section .text.__llvm_libc_mmix_clone_thread,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_clone_thread
.hidden __llvm_libc_mmix_clone_thread
.type __llvm_libc_mmix_clone_thread,@function
.hidden __llvm_libc_mmix_thread_start
__llvm_libc_mmix_clone_thread:
  SETL r237, SYS_clone
  TRAP 1, 0, 0
  BNZ r231, .Lparent_return
  LDOU r231, r230, 8
  PUSHJ r31, __llvm_libc_mmix_thread_start
.Lchild_returned:
  TRAP 255, 0, 0
  JMPB .Lchild_returned
.Lparent_return:
  POP 0, 0
.size __llvm_libc_mmix_clone_thread, .-__llvm_libc_mmix_clone_thread

// The result is already published in the control record; kernel status is zero.
// No ordinary call, POP, stack/TLS access or unmap is allowed beyond this
// boundary. Trap repeatedly if terminal exit returns.
.section .text.__llvm_libc_mmix_thread_exit,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_thread_exit
.hidden __llvm_libc_mmix_thread_exit
.type __llvm_libc_mmix_thread_exit,@function
__llvm_libc_mmix_thread_exit:
  SETL r231, 0
  SETL r237, SYS_exit
  TRAP 1, 0, 0
.Lexit_returned:
  TRAP 255, 0, 0
  JMPB .Lexit_returned
.size __llvm_libc_mmix_thread_exit, .-__llvm_libc_mmix_thread_exit

.section .note.GNU-stack,"",@progbits
