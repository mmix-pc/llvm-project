# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %s -o %t.o
# RUN: llvm-readobj --relocations --symbols %t.o | FileCheck %s --check-prefix=OBJ
# RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=BYTES
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=asm %s -o %t.s
# RUN: FileCheck %s --check-prefix=ASM < %t.s
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.roundtrip.o
# RUN: cmp %t.o %t.roundtrip.o
# RUN: llvm-mc -triple=mmix-unknown-linux -show-encoding %s | FileCheck %s --check-prefix=FIXUP

# Each offset names the whole instruction, whose immediate stays zero. Local
# TLS symbols must survive as symbols, even with a nonzero section offset.
# OBJ:      0x0 R_MMIX_TPREL_LO16 external 0x0
# OBJ-NEXT: 0x4 R_MMIX_TPREL_ML16 external 0x1
# OBJ-NEXT: 0x8 R_MMIX_TPREL_MH16 external 0xFFFFFFFFFFFFFFFF
# OBJ-NEXT: 0xC R_MMIX_TPREL_HI16 external 0x7FFFFFFFFFFFFFFF
# OBJ-NEXT: 0x10 R_MMIX_TPREL_LO16 external 0x8000000000000000
# OBJ-NEXT: 0x14 R_MMIX_TPREL_ML16 local_before 0xFFFFFFFFFFFFFFF8
# OBJ-NEXT: 0x18 R_MMIX_TPREL_MH16 .Llocal_after 0x8
# OBJ-NEXT: 0x1C R_MMIX_TPREL_HI16 weak_tls 0x0
# OBJ: Name: local_before
# OBJ: Binding: Local
# OBJ-NEXT: Type: TLS
# OBJ: Name: .Llocal_after
# OBJ: Binding: Local
# OBJ-NEXT: Type: TLS
# OBJ: Name: external
# OBJ: Type: TLS
# OBJ: Name: weak_tls
# OBJ: Binding: Weak
# OBJ-NEXT: Type: TLS

# BYTES:      e3 01 00 00
# BYTES:      e6 02 00 00
# BYTES:      e5 03 00 00
# BYTES:      e4 04 00 00
# BYTES:      e3 05 00 00
# BYTES:      e6 06 00 00
# BYTES:      e5 07 00 00
# BYTES:      e4 08 00 00

# ASM: SETL r1, %tprel_lo(external)
# ASM: INCML r2, %tprel_ml(external+1)
# ASM: INCMH r3, %tprel_mh(external-1)
# ASM: INCH r4, %tprel_hi(external+9223372036854775807)
# ASM: SETL r5, %tprel_lo(external-9223372036854775808)
# FIXUP: fixup A - offset: 0, value: %tprel_lo(external), kind: fixup_mmix_tprel_lo
# FIXUP: fixup A - offset: 0, value: %tprel_ml(external+1), kind: fixup_mmix_tprel_ml
# FIXUP: fixup A - offset: 0, value: %tprel_mh(external-1), kind: fixup_mmix_tprel_mh
# FIXUP: fixup A - offset: 0, value: %tprel_hi(external+9223372036854775807), kind: fixup_mmix_tprel_hi

.section .tdata,"awT",@progbits
.space 8
local_before:
.8byte 42

.text
SETL r1, %tprel_lo(external)
INCML r2, %tprel_ml(external + 1)
INCMH r3, %tprel_mh(external - 1)
INCH r4, %tprel_hi(external + 9223372036854775807)
SETL r5, %tprel_lo(external - 9223372036854775808)
INCML r6, %tprel_ml(local_before + -8)
INCMH r7, %tprel_mh(.Llocal_after + 8)
INCH r8, %tprel_hi(weak_tls)

.section .tbss,"awT",@nobits
.space 8
.Llocal_after:
.space 8
.weak weak_tls
weak_tls:
.space 8
