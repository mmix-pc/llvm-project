//===-- MMIX Linux stack protector ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <sys/syscall.h>

extern "C" {
// The Linux CRT owns the guard; compiler-rt's fixed fallback is not selected.
[[gnu::visibility("default")]] __UINT64_TYPE__ __stack_chk_guard;

[[gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_init_guard(const unsigned char *random) {
  // AT_RANDOM need not be aligned. Assemble the native big-endian octa without
  // a memcpy helper or a fixed-value fallback, including when all bytes are zero.
  __UINT64_TYPE__ value = 0;
  for (unsigned i = 0; i != 8; ++i)
    value = (value << 8) | random[i];
  __stack_chk_guard = value;
}

// FIXME: Use qualified Linux abnormal-termination handling when MMIX
// signal support is available; currently exit immediately with failure.
[[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_start_fail() {
  for (;;) {
    __llvm_libc_mmix_syscall(SYS_exit_group, 1, 0, 0, 0, 0, 0);
    __llvm_libc_mmix_syscall(SYS_exit, 1, 0, 0, 0, 0, 0);
  }
}

[[noreturn, gnu::visibility("default")]] void __stack_chk_fail() {
  __llvm_libc_mmix_linux_start_fail();
}
}
