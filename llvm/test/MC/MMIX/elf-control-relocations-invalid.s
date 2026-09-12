# RUN: split-file %s %t

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/symbol-differences.s \
# RUN:   -o %t/symbol-differences.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=DIFFERENCES
# RUN: not test -e %t/symbol-differences.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/unresolved-subtrahend.s \
# RUN:   -o %t/unresolved-subtrahend.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=SUBTRAHEND
# RUN: not test -e %t/unresolved-subtrahend.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/multiple-symbols.s \
# RUN:   -o %t/multiple-symbols.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MULTIPLE
# RUN: not test -e %t/multiple-symbols.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/symbolic-operations.s \
# RUN:   -o %t/symbolic-operations.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=OPERATIONS
# RUN: not test -e %t/symbolic-operations.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/modifier.s \
# RUN:   -o %t/modifier.o 2>&1 | FileCheck %s --check-prefix=MODIFIER
# RUN: not test -e %t/modifier.o

# RUN: not llvm-mc -triple=mmix -filetype=obj %t/geta-reclassification.s \
# RUN:   -o %t/geta-reclassification.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=GETA
# RUN: not test -e %t/geta-reclassification.o

# DIFFERENCES: symbol-differences.s:1:17: error: MMIX 19-bit terminal control relocation does not support symbol differences
# DIFFERENCES: symbol-differences.s:2:14: error: MMIX 27-bit terminal control relocation does not support symbol differences
# DIFFERENCES: symbol-differences.s:3:18: error: MMIX 19-bit terminal control relocation does not support symbol differences
# DIFFERENCES: symbol-differences.s:4:15: error: MMIX 27-bit terminal control relocation does not support symbol differences

# SUBTRAHEND: unresolved-subtrahend.s:3:14: error: symbol 'external' can not be undefined in a subtraction expression
# SUBTRAHEND: unresolved-subtrahend.s:4:11: error: symbol 'external' can not be undefined in a subtraction expression

# MULTIPLE: multiple-symbols.s:1:14: error: expected relocatable expression
# MULTIPLE: multiple-symbols.s:2:11: error: expected relocatable expression

# OPERATIONS: symbolic-operations.s:1:16: error: expected relocatable expression
# OPERATIONS: symbolic-operations.s:2:12: error: expected relocatable expression

# MODIFIER: modifier.s:1:9: error: expected '%geta' expression specifier

# GETA: geta-reclassification.s:1:1: error: '%geta' expression requires a GETA instruction
# GETA: geta-reclassification.s:2:1: error: '%geta' expression requires a GETA instruction

# Reject unresolved differences before the generic object writer can try to
# make an already PC-relative fixup PC-relative again. Keep the fixup identity.
#--- symbol-differences.s
BN r1, external - local
JMP external - local
BNB r1, external - local
JMPB external - local
local:
SWYM 0, 0, 0

#--- unresolved-subtrahend.s
local:
SWYM 0, 0, 0
BN r1, local - external
JMP local - external

#--- multiple-symbols.s
BN r1, first + second
JMP first + second

#--- symbolic-operations.s
BN r1, shifted << 2
JMP masked & 255

#--- modifier.s
BN r1, %other(external)

#--- geta-reclassification.s
BN r1, %geta(external)
JMP %geta(external)
