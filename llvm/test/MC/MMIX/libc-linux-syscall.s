# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %S/../../../../libc/src/__support/OSUtil/linux/mmix/syscall.S -o %t.o
# RUN: llvm-readobj --symbols --relocations %t.o | FileCheck %s --check-prefix=OBJ
# RUN: llvm-objdump --no-print-imm-hex -d %t.o | FileCheck %s --check-prefix=ASM
# RUN: llvm-objcopy --dump-section .text.__llvm_libc_mmix_syscall=%t.bin %t.o
# RUN: %python %S/Inputs/check-linux-syscall.py %t.bin

# OBJ: Relocations [
# OBJ-NEXT: ]
# OBJ: Name: __llvm_libc_mmix_syscall
# OBJ: Size: 40
# OBJ: Binding: Global
# OBJ: Type: Function
# OBJ: STV_HIDDEN

# ASM-LABEL: <__llvm_libc_mmix_syscall>:
# ASM-NEXT: OR r255, r231, 0
# ASM-NEXT: OR r231, r232, 0
# ASM-NEXT: OR r232, r233, 0
# ASM-NEXT: OR r233, r234, 0
# ASM-NEXT: OR r234, r235, 0
# ASM-NEXT: OR r235, r236, 0
# ASM-NEXT: OR r236, r237, 0
# ASM-NEXT: OR r237, r255, 0
# ASM-NEXT: TRAP 1, 0, 0
# ASM-NEXT: POP 0, 0
