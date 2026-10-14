//===-- MMIX Linux dynamic executable entry -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

extern "C" {
__attribute__((visibility("hidden"))) int main(int, char **, char **);
[[noreturn]] void __llvm_libc_mmix_dynamic_start(int (*)(int, char **, char **),
                                                 const void *);
[[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_dynamic_entry(const void *sp) {
  __llvm_libc_mmix_dynamic_start(main, sp);
}
}

// The interpreter restores the kernel SP and installs TP before AT_ENTRY.
// No incoming argument register or return address is part of this interface.
asm(".text\n"
    ".globl _start\n"
    ".type _start,@function\n"
    "_start:\n"
    "OR r231,r254,0\n"
    "PUSHJ r31,__llvm_libc_mmix_dynamic_entry\n"
    "TRAP 255,0,0\n"
    ".size _start,.-_start\n");
