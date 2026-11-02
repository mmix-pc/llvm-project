//===-- MMIX Linux private segmented unwind landing ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if !defined(__mmix__) || !defined(__linux__)
#error "The signal unwind landing requires MMIX Linux"
#endif
#if !defined(LIBC_MMIX_SIGNAL_UNWIND) || !defined(LIBC_MMIX_SIGNAL_CLEANUP)
#error "The signal unwind landing requires matched opt-in cleanup providers"
#endif

.section .text.__llvm_libc_mmix_signal_unwind_landing,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_signal_unwind_landing
.hidden __llvm_libc_mmix_signal_unwind_landing
.type __llvm_libc_mmix_signal_unwind_landing,@function
__llvm_libc_mmix_signal_unwind_landing:
  // The kernel restored the suspended parent's locals and FP/SP. A fresh call
  // supplies temporary stack space; never POP or return through its landing rJ.
  .hidden __llvm_libc_mmix_signal_unwind_resume
  PUSHJ r31, __llvm_libc_mmix_signal_unwind_resume
.Linvalid:
  TRAP 255, 0, 0
  JMPB .Linvalid
.size __llvm_libc_mmix_signal_unwind_landing, .-__llvm_libc_mmix_signal_unwind_landing
.section .note.GNU-stack,"",@progbits
