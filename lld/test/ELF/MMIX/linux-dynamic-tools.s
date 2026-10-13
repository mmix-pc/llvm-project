# REQUIRES: mmix
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %s -o %t.o
# RUN: ld.lld -m elf64mmix_linux -shared %t.o -o %t.so
# RUN: llvm-readobj --dyn-relocations %t.so | FileCheck %s --check-prefix=READOBJ
# RUN: llvm-objdump -R %t.so | FileCheck %s --check-prefix=OBJDUMP
# RUN: llvm-objdump -d --no-show-raw-insn %t.so | FileCheck %s --check-prefix=CODE
# RUN: llvm-objcopy --strip-all %t.so %t.stripped.so
# RUN: llvm-readobj --dyn-relocations %t.stripped.so | FileCheck %s --check-prefix=READOBJ
# RUN: llvm-objdump -R %t.stripped.so | FileCheck %s --check-prefix=OBJDUMP
# RUN: llvm-readobj --dyn-symbols %t.stripped.so | FileCheck %s --check-prefix=SYMBOLS
# READOBJ-DAG: R_MMIX_RELATIVE - 0x
# READOBJ-DAG: R_MMIX_GLOB_DAT external 0x0
# READOBJ-DAG: R_MMIX_64 external 0x0
# OBJDUMP-DAG: R_MMIX_RELATIVE
# OBJDUMP-DAG: R_MMIX_GLOB_DAT{{.*}}external
# OBJDUMP-DAG: R_MMIX_64{{.*}}external
# CODE: <function>:
# CODE-NEXT: {{.*}}SWYM 0x0, 0x0, 0x0
# CODE-NEXT: {{.*}}POP 0x0, 0x0
# SYMBOLS: Name: external
# SYMBOLS: Section: Undefined
# SYMBOLS: Name: function

.text
.p2align 3
.globl function
.type function,@function
function:
SWYM 0,0,0
POP 0,0
.mmix_got_pc64 external
.data
.p2align 3
local: .quad 1
.quad local
.quad external
