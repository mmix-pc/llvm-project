//===-- MMIX Linux raw syscall adapter -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if !defined(__mmix__) || !defined(__linux__)
#error "The MMIX Linux syscall adapter requires MMIX Linux"
#endif

// C arguments are (number, arg1, ..., arg6) in r231-r237. The kernel
// consumes r237 as the number and r231-r236 as arguments, and may clobber
// r231, r237 and r255. It preserves rJ and the logical register stack on
// ordinary return, so this leaf needs neither a software frame nor an rJ save.
.section .text.__llvm_libc_mmix_syscall,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_syscall
.hidden __llvm_libc_mmix_syscall
.type __llvm_libc_mmix_syscall,@function
__llvm_libc_mmix_syscall:
  OR r255, r231, 0
  OR r231, r232, 0
  OR r232, r233, 0
  OR r233, r234, 0
  OR r234, r235, 0
  OR r235, r236, 0
  OR r236, r237, 0
  OR r237, r255, 0
  TRAP 1, 0, 0
  POP 0, 0
.size __llvm_libc_mmix_syscall, .-__llvm_libc_mmix_syscall

.section .note.GNU-stack,"",@progbits
