//===-- MMIX Linux jump preparation ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "restore.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include <asm/rstack.h>
#include <asm/unistd.h>

extern "C" [[noreturn]] void __llvm_libc_mmix_jump_landing();
extern "C" [[noreturn]] void __llvm_libc_mmix_jump_fail();

static_assert(sizeof(mmix_rstack_jump) == 48 && alignof(mmix_rstack_jump) == 8);
static_assert(__builtin_offsetof(mmix_rstack_jump, chain_id) == 0);
static_assert(__builtin_offsetof(mmix_rstack_jump, landing) == 8);
static_assert(__builtin_offsetof(mmix_rstack_jump, environment) == 16);
static_assert(__builtin_offsetof(mmix_rstack_jump, result) == 24);
static_assert(__builtin_offsetof(mmix_rstack_jump, flags) == 32);
static_assert(__builtin_offsetof(mmix_rstack_jump, sigmask) == 40);
static_assert(sizeof(__jmp_buf) == 304 && alignof(__jmp_buf) == 8);
static_assert(__builtin_offsetof(__jmp_buf, frame_pointer) == 0);
static_assert(__builtin_offsetof(__jmp_buf, return_address) == 8);
static_assert(__builtin_offsetof(__jmp_buf, stack_pointer) == 16);
static_assert(__builtin_offsetof(__jmp_buf, register_stack_offset) == 24);
static_assert(__builtin_offsetof(__jmp_buf, locals) == 32);
static_assert(__builtin_offsetof(__jmp_buf, chain_id) == 280);
static_assert(__builtin_offsetof(__jmp_buf, signal_mask) == 288);
static_assert(__builtin_offsetof(__jmp_buf, save_mask) == 296);

namespace LIBC_NAMESPACE_DECL {

[[noreturn]] void restore_jump(const __jmp_buf *env, int value, bool restore_mask) {
  const bool set_mask = restore_mask && env->save_mask != 0;
  const mmix_rstack_jump request = {
      env->chain_id,
      reinterpret_cast<unsigned long>(&__llvm_libc_mmix_jump_landing),
      reinterpret_cast<unsigned long>(env),
      static_cast<unsigned long>(static_cast<long>(value ? value : 1)),
      set_mask ? MMIX_RSTACK_JUMP_SETMASK : 0UL,
      set_mask ? env->signal_mask : 0};

  // Even same-chain jumps require preparation. Success consumes the copied
  // request and enters the landing pad, never returning through this stack.
  syscall_impl(__NR_mmix_rstack_jump, reinterpret_cast<long>(&request));
  // Any return, including an unexpected zero, is a failed transfer.
  __llvm_libc_mmix_jump_fail();
}

} // namespace LIBC_NAMESPACE_DECL
