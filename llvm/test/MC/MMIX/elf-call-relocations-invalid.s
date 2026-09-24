# RUN: split-file %s %t

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/local-targets.s \
# RUN:   -o %t/local-targets.o 2>&1 | FileCheck %s --check-prefix=LOCAL
# RUN: not test -e %t/local-targets.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/symbol-difference.s \
# RUN:   -o %t/symbol-difference.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=DIFFERENCE
# RUN: not test -e %t/symbol-difference.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/multiple-symbols.s \
# RUN:   -o %t/multiple-symbols.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MULTIPLE
# RUN: not test -e %t/multiple-symbols.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/symbolic-operation.s \
# RUN:   -o %t/symbolic-operation.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=OPERATION
# RUN: not test -e %t/symbolic-operation.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/modifiers.s \
# RUN:   -o %t/modifiers.o 2>&1 | FileCheck %s --check-prefix=MODIFIER
# RUN: not test -e %t/modifiers.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/raw-relocations.s \
# RUN:   -o %t/raw-relocations.o 2>&1 | FileCheck %s --check-prefix=RAW
# RUN: not test -e %t/raw-relocations.o

# LOCAL: local-targets.s:2:11: error: MMIX direct call target is not instruction aligned
# LOCAL: local-targets.s:7:11: error: MMIX direct call target direction does not match instruction
# LOCAL: local-targets.s:11:12: error: MMIX direct call target direction does not match instruction
# LOCAL: local-targets.s:16:11: error: MMIX direct call target is out of range
# LOCAL: local-targets.s:20:12: error: MMIX direct call target is out of range

# DIFFERENCE: symbol-difference.s:1:20: error: MMIX stubbable call relocations do not support symbol differences
# MULTIPLE: multiple-symbols.s:1:17: error: expected relocatable expression
# OPERATION: symbolic-operation.s:1:19: error: expected relocatable expression
# MODIFIER: modifiers.s:1:12: error: unknown MMIX expression specifier
# MODIFIER: modifiers.s:2:1: error: '%geta' expression requires a GETA instruction

# R_MMIX_PUSHJ_STUBBABLE is owned by PUSHJ/PUSHJB fixups and cannot be
# requested directly on data, another instruction class, or even a call site.
# RAW-COUNT-3: error: unknown relocation name

#--- local-targets.s
call_misaligned:
PUSHJ r1, misaligned_target
.set misaligned_target, call_misaligned + 2

call_forward_direction:
SWYM 0, 0, 0
PUSHJ r2, before_forward
.set before_forward, call_forward_direction

call_backward_direction:
PUSHJB r3, after_backward
SWYM 0, 0, 0
.set after_backward, call_backward_direction + 8

call_forward_range:
PUSHJ r4, forward_out_of_range
.set forward_out_of_range, call_forward_range + 262144

call_backward_range:
PUSHJB r5, backward_out_of_range
.set backward_out_of_range, call_backward_range - 262148

#--- symbol-difference.s
PUSHJ r1, external - local
local:
SWYM 0, 0, 0

#--- multiple-symbols.s
PUSHJ r1, first + second

#--- symbolic-operation.s
PUSHJ r1, shifted << 2

#--- modifiers.s
PUSHJ r1, %other(external)
PUSHJ r2, %geta(external)

#--- raw-relocations.s
.data
.reloc ., R_MMIX_PUSHJ_STUBBABLE, external
.text
branch:
BN r1, 0
.reloc branch, R_MMIX_PUSHJ_STUBBABLE, external
call:
PUSHJ r2, 0
.reloc call, R_MMIX_PUSHJ_STUBBABLE, external
