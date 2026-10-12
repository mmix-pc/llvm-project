# RUN: split-file %s %t
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/expressions.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXPR
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/symbols.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=SYMBOL
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/alignment.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ALIGN
# RUN: not llvm-mc -triple=mmix-unknown-unknown -filetype=obj %t/profile.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROFILE
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=asm --output-asm-variant=1 %t/profile.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROFILE
# EXPR: error: MMIX GOT expression requires a symbol
# EXPR-COUNT-2: error: expected integer MMIX GOT addend
# EXPR-COUNT-2: error: MMIX GOT addend is outside signed 64-bit range
# EXPR: error: expected newline
# SYMBOL-COUNT-2: error: MMIX GOT literal requires one non-TLS symbol
# ALIGN: error: MMIX GOT literal must be eight-byte aligned
# PROFILE: error: MMIX GOT literals require Linux canonical assembly

#--- expressions.s
.mmix_got_pc64 42
.mmix_got_pc64 a-b
.mmix_got_pc64 a+b
.mmix_got_pc64 a+9223372036854775808
.mmix_got_pc64 a-9223372036854775809
.mmix_got_pc64 a+1+2
#--- symbols.s
.mmix_got_pc64 absolute
.mmix_got_pc64 tls
.set absolute,42
.type tls,@tls_object
#--- alignment.s
.byte 0
.mmix_got_pc64 external
#--- profile.s
.mmix_got_pc64 external
