; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
; RUN:   -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
; RUN:   -stop-after=postrapseudos %s -o - | FileCheck %s --check-prefix=POSTRA
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=asm \
; RUN:   %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
; RUN:   %s -o %t.o
; RUN: llvm-readobj --relocations --expand-relocs %t.o \
; RUN:   | FileCheck %s --check-prefix=RELOCS --implicit-check-not=R_MMIX_
; RUN: llvm-readobj --symbols %t.o \
; RUN:   | FileCheck %s --check-prefix=SYMBOLS
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
; RUN:   | FileCheck %s --check-prefix=OBJ --implicit-check-not='<unknown>'

; RELOCS:      Section {{.*}} .rela.text {
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x2C
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: .text.separate
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x4C
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: declared
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x70
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: visible_target
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0x90
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: weak_declared
; RELOCS-NEXT:     Addend: 0x0
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Relocation {
; RELOCS-NEXT:     Offset: 0xB0
; RELOCS-NEXT:     Type: R_MMIX_GETA (13)
; RELOCS-NEXT:     Symbol: declared
; RELOCS-NEXT:     Addend: 0xFFFFFFFFFFFFFFF4
; RELOCS-NEXT:   }
; RELOCS-NEXT: }

; SYMBOLS:      Name: .text.separate
; SYMBOLS:      Binding: Local
; SYMBOLS-NEXT: Type: Section
; SYMBOLS:      Section: .text.separate
; SYMBOLS:      Name: declared
; SYMBOLS:      Binding: Global
; SYMBOLS-NEXT: Type: None
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: visible_target
; SYMBOLS:      Binding: Global
; SYMBOLS-NEXT: Type: Function
; SYMBOLS:      Section: .text
; SYMBOLS:      Name: weak_declared
; SYMBOLS:      Binding: Weak
; SYMBOLS-NEXT: Type: None
; SYMBOLS:      Section: Undefined

; Same-section calls resolve after object layout, retaining the selected
; direction without a relocation.
; OBJ-LABEL: <call_backward>:
; OBJ:       {{.*}} PUSHJB r31, -2
; OBJ-LABEL: <call_forward>:
; OBJ:       {{.*}} PUSHJ r31, 3

; OBJ-LABEL: <call_other_section>:
; OBJ:       {{.*}} GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA .text.separate
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
; OBJ-LABEL: <call_declaration>:
; OBJ:       {{.*}} GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA declared
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
; OBJ-LABEL: <call_visible>:
; OBJ:       {{.*}} GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA visible_target
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
; OBJ-LABEL: <call_weak>:
; OBJ:       {{.*}} GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA weak_declared
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
; OBJ-LABEL: <call_with_addend>:
; OBJ:       {{.*}} GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA declared-0xc
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0

; Register-indirect calls remain outside the symbolic relocation path.
; OBJ-LABEL: <call_indirect>:
; OBJ:       {{.*}} PUSHGO r31, r231, 0

target triple = "mmix-unknown-elf"

declare void @declared()
declare extern_weak void @weak_declared()

define internal void @backward_target() nounwind {
  ret void
}

; Stable same-section callees retain the layout-selected direct-call path.
; ISEL-LABEL: name: call_backward
; ISEL-NOT:   LOAD_CALL_ADDR
; ISEL:       CALL_STATE @backward_target, csr_mmix
; POSTRA-LABEL: name: call_backward
; POSTRA-NOT:   LOAD_CALL_ADDR
; POSTRA:       PseudoPUSHJ $r31, @backward_target, csr_mmix
; ASM-LABEL: call_backward:
; ASM:       PUSHJB r31, backward_target
define void @call_backward() nounwind {
  call void @backward_target()
  ret void
}

; ISEL-LABEL: name: call_forward
; ISEL-NOT:   LOAD_CALL_ADDR
; ISEL:       CALL_STATE @forward_target, csr_mmix
; POSTRA-LABEL: name: call_forward
; POSTRA-NOT:   LOAD_CALL_ADDR
; POSTRA:       PseudoPUSHJ $r31, @forward_target, csr_mmix
; ASM-LABEL: call_forward:
; ASM:       PUSHJ r31, forward_target
define void @call_forward() nounwind {
  call void @forward_target()
  ret void
}

define internal void @forward_target() nounwind {
  ret void
}

define internal void @section_target() nounwind section ".text.separate" {
  ret void
}

; Inter-section, declared, externally visible, and weak callees retain a
; symbolic direct-call operand alongside the address register in every mode.
; ISEL-LABEL: name: call_other_section
; ISEL:       [[SECTION_ADDR:%[0-9]+]]:{{[^ ]+}} = LOAD_CALL_ADDR @section_target
; ISEL-NEXT:  DIRECT_CALL_STATE @section_target, killed [[SECTION_ADDR]], csr_mmix
; POSTRA-LABEL: name: call_other_section
; POSTRA:       [[SECTION_REG:\$r[0-9]+]] = LOAD_CALL_ADDR @section_target
; POSTRA-NEXT:  PseudoDirectCall $r31, @section_target, killed [[SECTION_REG]], csr_mmix
; ASM-LABEL: call_other_section:
; ASM:       GETA [[SECTION_TEXT:r[0-9]+]], %geta(section_target)
; ASM:       PUSHGO r31, [[SECTION_TEXT]], 0
define void @call_other_section() nounwind {
  call void @section_target()
  ret void
}

; ISEL-LABEL: name: call_declaration
; ISEL:       [[DECL_ADDR:%[0-9]+]]:{{[^ ]+}} = LOAD_CALL_ADDR @declared
; ISEL-NEXT:  DIRECT_CALL_STATE @declared, killed [[DECL_ADDR]], csr_mmix
; POSTRA-LABEL: name: call_declaration
; POSTRA:       PseudoDirectCall $r31, @declared, killed $r{{[0-9]+}}, csr_mmix
; ASM-LABEL: call_declaration:
; ASM:       GETA [[DECL_TEXT:r[0-9]+]], %geta(declared)
; ASM:       PUSHGO r31, [[DECL_TEXT]], 0
define void @call_declaration() nounwind {
  call void @declared()
  ret void
}

define dso_preemptable void @visible_target() nounwind {
  ret void
}

; ISEL-LABEL: name: call_visible
; ISEL:       DIRECT_CALL_STATE @visible_target, {{.*}}csr_mmix
; POSTRA-LABEL: name: call_visible
; POSTRA:       PseudoDirectCall $r31, @visible_target, {{.*}}csr_mmix
define void @call_visible() nounwind {
  call void @visible_target()
  ret void
}

; ISEL-LABEL: name: call_weak
; ISEL:       DIRECT_CALL_STATE @weak_declared, {{.*}}csr_mmix
; POSTRA-LABEL: name: call_weak
; POSTRA:       PseudoDirectCall $r31, @weak_declared, {{.*}}csr_mmix
define void @call_weak() nounwind {
  call void @weak_declared()
  ret void
}

; A signed constant attached to a function symbol survives both producer
; boundaries. The eventual ELF helper therefore receives one S+A expression.
; ISEL-LABEL: name: call_with_addend
; ISEL:       [[ADDEND_ADDR:%[0-9]+]]:{{[^ ]+}} = LOAD_CALL_ADDR @declared - 12
; ISEL-NEXT:  DIRECT_CALL_STATE @declared - 12, killed [[ADDEND_ADDR]], csr_mmix
; POSTRA-LABEL: name: call_with_addend
; POSTRA:       PseudoDirectCall $r31, @declared - 12, killed $r{{[0-9]+}}, csr_mmix
; ASM-LABEL: call_with_addend:
; ASM:       GETA [[ADDEND_TEXT:r[0-9]+]], %geta(declared-12)
; ASM:       PUSHGO r31, [[ADDEND_TEXT]], 0
define void @call_with_addend() nounwind {
  call void getelementptr (i8, ptr @declared, i64 -12)()
  ret void
}

; Register-indirect calls never enter the symbolic direct-call path.
; ISEL-LABEL: name: call_indirect
; ISEL-NOT:   LOAD_CALL_ADDR
; ISEL-NOT:   DIRECT_CALL_STATE
; ISEL:       CALL_STATE %{{[0-9]+}}, csr_mmix
; POSTRA-LABEL: name: call_indirect
; POSTRA-NOT:   LOAD_CALL_ADDR
; POSTRA-NOT:   PseudoDirectCall
; POSTRA:       PseudoPUSHGO $r31, killed $r231, 0, csr_mmix
; ASM-LABEL: call_indirect:
; ASM:       PUSHGO r31, r231, 0
define void @call_indirect(ptr %callee) nounwind {
  call void %callee()
  ret void
}
