; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
; RUN:   -stop-after=postrapseudos %s -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=asm \
; RUN:   %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
; RUN:   %s -o %t.o
; RUN: llvm-readobj --relocations --expand-relocs %t.o \
; RUN:   | FileCheck %s --check-prefix=RELOCS --implicit-check-not=R_MMIX_
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
; RUN:   | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs -filetype=obj %s -o %t.linux.o
; RUN: llvm-readobj --sections --relocations --expand-relocs %t.linux.o \
; RUN:   | FileCheck %s --check-prefix=RELOCS --implicit-check-not=R_MMIX_ --implicit-check-not=.MMIX.reg_contents
; RUN: llvm-objdump --no-print-imm-hex -dr %t.linux.o | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM

; Constant-pool object emission is covered by elf-constant-pool-address.mir.
; The dormant jump-table address operand remains structurally covered by
; address-materialization.mir.

; RELOCS:      Section {{.*}} .rela.text {
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x4
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: .bss
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x18
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: external_data
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x2C
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: .text
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x40
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: external_function
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x54
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: .bss
; RELOCS-NEXT:     Addend: 0x1234
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x70
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: external_data
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x88
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: .text
; RELOCS-NEXT:     Addend: 0x88
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0xA0
; RELOCS-NEXT:     Type: R_MMIX_PUSHJ_STUBBABLE (36)
; RELOCS-NEXT:     Symbol: external_function
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT: }

; OBJ-LABEL: <defined_data_address>:
; OBJ-NEXT:  {{.*}} GETA r231, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA .bss
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} POP 0, 0

; OBJ-LABEL: <external_data_address>:
; OBJ-NEXT:  {{.*}} GETA r231, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA external_data
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} POP 0, 0

; OBJ-LABEL: <defined_function_address>:
; OBJ-NEXT:  {{.*}} GETA r231, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA .text
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} POP 0, 0

; OBJ-LABEL: <external_function_address>:
; OBJ-NEXT:  {{.*}} GETA r231, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA external_function
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} POP 0, 0

; OBJ-LABEL: <positive_address_offset>:
; OBJ-NEXT:  {{.*}} GETA r231, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA .bss+0x1234
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} POP 0, 0

; OBJ-LABEL: <negative_address_offset>:
; OBJ-NEXT:  {{.*}} SETL r250, 4660
; OBJ-NEXT:  {{.*}} NEGU r250, 0, r250
; OBJ-NEXT:  {{.*}} GETA r249, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA external_data
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} ADDU r231, r249, r250
; OBJ-NEXT:  {{.*}} POP 0, 0

; OBJ-LABEL: <block_address>:
; OBJ-NEXT:  {{.*}} GETA r231, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA .text+0x88
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} SWYM 0, 0, 0
; OBJ-NEXT:  {{.*}} POP 0, 0

; A function address uses GETA, while direct and indirect calls retain their
; separately owned stubbable and register-indirect paths.
; OBJ-LABEL: <direct_call>:
; OBJ:       {{.*}} PUSHJ r31, 0
; OBJ-NEXT:  {{.*}} R_MMIX_PUSHJ_STUBBABLE external_function
; OBJ-LABEL: <indirect_call>:
; OBJ:       {{.*}} PUSHGO r31, r231, 0

target triple = "mmix-unknown-elf"

@defined_data = internal global [8 x i8] zeroinitializer, align 8
@external_data = external global i8

declare void @external_function()

define internal void @defined_function() nounwind {
  ret void
}

; MIR-LABEL: name: defined_data_address
; MIR:       $r231 = LOAD_ADDR @defined_data
; ASM-LABEL: defined_data_address:
; ASM:       GETA r231, %geta(defined_data)
define ptr @defined_data_address() nounwind {
  ret ptr @defined_data
}

; MIR-LABEL: name: external_data_address
; MIR:       $r231 = LOAD_ADDR @external_data
define ptr @external_data_address() nounwind {
  ret ptr @external_data
}

; MIR-LABEL: name: defined_function_address
; MIR:       $r231 = LOAD_ADDR @defined_function
define ptr @defined_function_address() nounwind {
  ret ptr @defined_function
}

; MIR-LABEL: name: external_function_address
; MIR:       $r231 = LOAD_ADDR @external_function
define ptr @external_function_address() nounwind {
  ret ptr @external_function
}

; A folded positive byte offset remains attached to the static-address pseudo.
; The text and object paths preserve one complete S+A expression.
; MIR-LABEL: name: positive_address_offset
; MIR:       $r231 = LOAD_ADDR @defined_data + 4660
; ASM-LABEL: positive_address_offset:
; ASM:       GETA r231, %geta(defined_data+4660)
define ptr @positive_address_offset() nounwind {
  ret ptr getelementptr (i8, ptr @defined_data, i64 4660)
}

; The current lowering emits this negative offset as explicit integer
; arithmetic, preserving the complete signed value outside the relocation.
; MIR-LABEL: name: negative_address_offset
; MIR:       [[NEGATIVE_OFFSET:\$r[0-9]+]] = SETL 4660
; MIR-NEXT:  [[NEGATIVE_OFFSET]] = NEGU 0, [[NEGATIVE_OFFSET]]
; MIR-NEXT:  [[NEGATIVE_BASE:\$r[0-9]+]] = LOAD_ADDR @external_data
; MIR-NEXT:  $r231 = ADDU killed [[NEGATIVE_BASE]], killed [[NEGATIVE_OFFSET]]
define ptr @negative_address_offset() nounwind {
  ret ptr getelementptr (i8, ptr @external_data, i64 -4660)
}

; MIR-LABEL: name: block_address
; MIR:       $r231 = LOAD_ADDR blockaddress(@block_address, %ir-block.target)
define ptr @block_address() nounwind {
entry:
  br label %target

target:
  ret ptr blockaddress(@block_address, %target)
}

; Unresolved direct calls preserve both their symbolic callee and the scratch
; register used by the relocatable text fallback.
; MIR-LABEL: name: direct_call
; MIR-NOT:   LOAD_ADDR
; MIR:       [[CALL_SCRATCH:\$r[0-9]+]] = LOAD_CALL_ADDR @external_function
; MIR-NEXT:  PseudoDirectCall $r31, @external_function, killed [[CALL_SCRATCH]], csr_mmix
; ASM-LABEL: direct_call:
; ASM:       GETA [[CALL_TARGET:r[0-9]+]], %geta(external_function)
; ASM:       PUSHGO r31, [[CALL_TARGET]], 0
; ASM-NOT:   PUSHJ
define void @direct_call() nounwind {
  call void @external_function()
  ret void
}

; Indirect calls already hold their target in a register.
; MIR-LABEL: name: indirect_call
; MIR-NOT:   LOAD_ADDR
; MIR-NOT:   LOAD_CALL_ADDR
; MIR:       PseudoPUSHGO
define void @indirect_call(ptr %callee) nounwind {
  call void %callee()
  ret void
}
