# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %s -o %t.o
# RUN: llvm-readobj --relocations --symbols %t.o | FileCheck %s
# RUN: llvm-readobj --sections --section-data %t.o | FileCheck %s --check-prefix=DATA
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=asm %s -o %t.s
# RUN: FileCheck %s --check-prefix=ASM < %t.s
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.roundtrip.o
# RUN: cmp %t.o %t.roundtrip.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=null %s

# These are addresses of linker-owned descriptors/slots, not TLS object
# offsets. Addends belong to those addresses and must survive unchanged.
.section .rodata,"a",@progbits
.mmix_tlsgd_pc64 external
.mmix_tlsld_pc64 .Lbefore
.mmix_gottprel_pc64 external + 8
.mmix_tlsgd_pc64 .Lafter - 8
.mmix_tlsld_pc64 .Lafter + 9223372036854775807
.mmix_gottprel_pc64 external - 9223372036854775808
.mmix_tlsgd_pc64 weak_tls
.mmix_tlsld_pc64 .Lbefore + -8
.mmix_gottprel_pc64 .Lafter - -8
.mmix_tlsgd_pc64 explicit_tls
.mmix_tlsld_pc64 .Lbefore - 9223372036854775808
.mmix_gottprel_pc64 explicit_tls + 9223372036854775807
.type explicit_tls,@tls_object
.weak weak_tls

# CHECK:      0x0 R_MMIX_TLSGD_PC64 external 0x0
# CHECK-NEXT: 0x8 R_MMIX_TLSLD_PC64 .Lbefore 0x0
# CHECK-NEXT: 0x10 R_MMIX_GOTTPREL_PC64 external 0x8
# CHECK-NEXT: 0x18 R_MMIX_TLSGD_PC64 .Lafter 0xFFFFFFFFFFFFFFF8
# CHECK-NEXT: 0x20 R_MMIX_TLSLD_PC64 .Lafter 0x7FFFFFFFFFFFFFFF
# CHECK-NEXT: 0x28 R_MMIX_GOTTPREL_PC64 external 0x8000000000000000
# CHECK-NEXT: 0x30 R_MMIX_TLSGD_PC64 weak_tls 0x0
# CHECK-NEXT: 0x38 R_MMIX_TLSLD_PC64 .Lbefore 0xFFFFFFFFFFFFFFF8
# CHECK-NEXT: 0x40 R_MMIX_GOTTPREL_PC64 .Lafter 0x8
# CHECK-NEXT: 0x48 R_MMIX_TLSGD_PC64 explicit_tls 0x0
# CHECK-NEXT: 0x50 R_MMIX_TLSLD_PC64 .Lbefore 0x8000000000000000
# CHECK-NEXT: 0x58 R_MMIX_GOTTPREL_PC64 explicit_tls 0x7FFFFFFFFFFFFFFF

# Check same-section definitions as well: MC must retain their symbol identity
# rather than resolve the reference or replace it with a section symbol.
.section .tdata,"awT",@progbits
.space 8
.Lbefore:
.8byte 42
.mmix_tlsgd_pc64 .Lbefore + 8
.mmix_tlsld_pc64 .Lafter
.mmix_gottprel_pc64 .Lafter - 8
.Lafter:
.8byte 9
# CHECK:      0x10 R_MMIX_TLSGD_PC64 .Lbefore 0x8
# CHECK-NEXT: 0x18 R_MMIX_TLSLD_PC64 .Lafter 0x0
# CHECK-NEXT: 0x20 R_MMIX_GOTTPREL_PC64 .Lafter 0xFFFFFFFFFFFFFFF8
# CHECK: Name: .Lbefore
# CHECK: Value: 0x8
# CHECK: Binding: Local
# CHECK-NEXT: Type: TLS
# CHECK: Name: .Lafter
# CHECK: Value: 0x28
# CHECK: Binding: Local
# CHECK-NEXT: Type: TLS
# CHECK: Name: external
# CHECK: Type: TLS
# CHECK: Name: weak_tls
# CHECK: Binding: Weak
# CHECK-NEXT: Type: TLS
# CHECK: Name: explicit_tls
# CHECK: Type: TLS

# DATA: Name: .rodata
# DATA: Size: 96
# DATA: AddressAlignment: 8
# DATA: SectionData (
# DATA-NEXT: 0000: 00000000 00000000 00000000 00000000
# DATA-NEXT: 0010: 00000000 00000000 00000000 00000000
# DATA-NEXT: 0020: 00000000 00000000 00000000 00000000
# DATA-NEXT: 0030: 00000000 00000000 00000000 00000000
# DATA-NEXT: 0040: 00000000 00000000 00000000 00000000
# DATA-NEXT: 0050: 00000000 00000000 00000000 00000000
# ASM: .mmix_tlsgd_pc64 external
# ASM-NEXT: .mmix_tlsld_pc64 .Lbefore
# ASM-NEXT: .mmix_gottprel_pc64 external+8
# ASM-NEXT: .mmix_tlsgd_pc64 .Lafter-8
# ASM-NEXT: .mmix_tlsld_pc64 .Lafter+9223372036854775807
# ASM-NEXT: .mmix_gottprel_pc64 external-9223372036854775808
