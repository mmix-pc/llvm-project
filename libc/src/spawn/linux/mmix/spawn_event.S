//===-- MMIX returns-twice spawn CREATE leaf ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if !defined(__mmix__) || !defined(__linux__)
#error "The spawn event transport requires MMIX Linux"
#endif

#include <asm/spawn.h>
#include <asm/unistd.h>

// C arguments: request in r231, sizeof(request) in r232. The copied child
// resumes after TRAP with r231 zero. Neither path changes rJ, the software
// stack, or the local register window before the normal matching POP.
.section .text.__llvm_libc_mmix_spawn_event_create,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_spawn_event_create
.hidden __llvm_libc_mmix_spawn_event_create
.type __llvm_libc_mmix_spawn_event_create,@function
__llvm_libc_mmix_spawn_event_create:
  OR r233, r232, 0
  OR r232, r231, 0
  SETL r231, MMIX_SPAWN_CREATE
  SETL r234, 0
  SETL r235, 0
  SETL r236, 0
  SETL r237, __NR_mmix_spawn
  TRAP 1, 0, 0
  POP 0, 0
.size __llvm_libc_mmix_spawn_event_create, .-__llvm_libc_mmix_spawn_event_create

.section .note.GNU-stack,"",@progbits
