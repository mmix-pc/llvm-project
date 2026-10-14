# RUN: split-file %s %t
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/expressions.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXPR
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/symbols.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=SYMBOL
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/alignment.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ALIGN
# RUN: not llvm-mc -triple=mmix-unknown-unknown -filetype=obj %t/profile.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROFILE
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=asm --output-asm-variant=1 %t/profile.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROFILE
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj --output-asm-variant=1 %t/profile.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=MMIXAL-OBJ
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/instruction.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=INSTRUCTION

# EXPR-COUNT-3: error: MMIX TLS expression requires a symbol
# EXPR-COUNT-2: error: expected integer MMIX TLS addend
# EXPR-COUNT-4: error: MMIX TLS addend is outside signed 64-bit range
# EXPR-COUNT-2: error: expected newline
# EXPR: error: MMIX TLS addend is outside signed 64-bit range
# SYMBOL-COUNT-7: error: MMIX TLS relocation requires a TLS symbol
# ALIGN-COUNT-4: error: MMIX TLS literal must be eight-byte aligned
# PROFILE-COUNT-4: error: MMIX TLS literals require Linux canonical assembly
# MMIXAL-OBJ: error: MMIXAL complete-source emission is not available
# INSTRUCTION-COUNT-3: error: unknown MMIX expression specifier

#--- expressions.s
.mmix_tlsgd_pc64 42
.mmix_tlsld_pc64 %tprel_lo(a)
.mmix_gottprel_pc64 (a)
.mmix_tlsgd_pc64 a-b
.mmix_tlsld_pc64 a+b
.mmix_tlsgd_pc64 a+9223372036854775808
.mmix_tlsld_pc64 a-9223372036854775809
.mmix_gottprel_pc64 a+18446744073709551616
.mmix_gottprel_pc64 a+0xffffffffffffffff
.mmix_tlsgd_pc64 a+1+2
.mmix_tlsld_pc64 a@GOT
.mmix_dtpoff64 a+9223372036854775808

#--- symbols.s
.mmix_tlsgd_pc64 object
.mmix_tlsld_pc64 function
.mmix_gottprel_pc64 ordinary
.mmix_tlsgd_pc64 absolute
.mmix_tlsld_pc64 common
.mmix_gottprel_pc64 tls_in_data
.mmix_dtpoff64 object
.type object,@object
.type function,@function
.set absolute,42
.comm common,8,8
.data
ordinary:
.8byte 0
.type tls_in_data,@tls_object
tls_in_data:
.8byte 0

#--- alignment.s
.byte 0
.mmix_tlsgd_pc64 a
.p2align 3
.byte 0
.mmix_tlsld_pc64 b
.p2align 3
.byte 0
.mmix_gottprel_pc64 c
.p2align 3
.byte 0
.mmix_dtpoff64 d

#--- profile.s
.mmix_tlsgd_pc64 a
.mmix_tlsld_pc64 b
.mmix_gottprel_pc64 c
.mmix_dtpoff64 d

#--- instruction.s
SETL r1, %tlsgd_pc64(a)
SETL r1, %tlsld_pc64(b)
SETL r1, %gottprel_pc64(c)
