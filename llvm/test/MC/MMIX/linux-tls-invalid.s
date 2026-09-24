# RUN: split-file %s %t
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/opcodes.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPCODE
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=asm %t/opcodes.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPCODE
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/expressions.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXPR
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/range.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANGE
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/symbols.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=SYMBOL
# RUN: not llvm-mc -triple=mmix-unknown-unknown -filetype=obj %t/profile.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROFILE
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=asm --output-asm-variant=1 %t/profile.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROFILE
# RUN: not llvm-mc -triple=mmix-unknown-linux -filetype=obj --output-asm-variant=1 %t/profile.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=MMIXAL-OBJ

# OPCODE-COUNT-5: error: MMIX TLS modifier does not match the instruction's wyde operand
# OPCODE: error: invalid operand for instruction
# OPCODE: error: invalid operand for instruction
# EXPR: error: MMIX TLS expression requires a symbol
# EXPR: error: expected integer MMIX TLS addend
# EXPR: error: expected integer MMIX TLS addend
# EXPR: error: MMIX TLS expression requires a symbol
# EXPR: error: expected ')' after MMIX TLS symbol and addend
# EXPR: error: expected comma
# EXPR: error: expected ')' after MMIX TLS symbol and addend
# RANGE-COUNT-4: error: MMIX TLS addend is outside signed 64-bit range
# SYMBOL-COUNT-5: error: MMIX TLS relocation requires a TLS symbol
# PROFILE: error: MMIX TLS expressions require Linux canonical assembly
# MMIXAL-OBJ: error: MMIXAL complete-source emission is not available

#--- opcodes.s
SETL r1, %tprel_hi(tls)
INCML r1, %tprel_mh(tls)
INCMH r1, %tprel_ml(tls)
INCH r1, %tprel_lo(tls)
GETA r1, %tprel_lo(tls)
ADDU r1, r2, %tprel_lo(tls)
SETL %tprel_lo(tls), 0

#--- expressions.s
SETL r1, %tprel_lo(7)
SETL r1, %tprel_lo(first - second)
SETL r1, %tprel_lo(first + second)
SETL r1, %tprel_lo(%tprel_lo(tls))
SETL r1, %tprel_lo(tls * 2)
SETL r1, %tprel_lo(tls) + 1
SETL r1, %tprel_lo(tls + 1 + 2)

#--- range.s
SETL r1, %tprel_lo(tls + 9223372036854775808)
SETL r1, %tprel_lo(tls - 9223372036854775809)
SETL r1, %tprel_lo(tls + 18446744073709551616)
SETL r1, %tprel_lo(tls + 0xffffffffffffffff)

#--- symbols.s
.type object, @object
SETL r1, %tprel_lo(object)
INCML r1, %tprel_ml(function)
INCMH r1, %tprel_mh(ordinary)
INCH r1, %tprel_hi(absolute)
SETL r1, %tprel_lo(common)
.type function, @function
.data
ordinary:
.8byte 0
.set absolute, 42
.comm common, 8, 8

#--- profile.s
SETL r1, %tprel_lo(tls)
