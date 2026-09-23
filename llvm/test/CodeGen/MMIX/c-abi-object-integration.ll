; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=asm \
; RUN:   %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
; RUN:   %s -o %t.o
; RUN: llvm-readobj --sections --section-data --symbols \
; RUN:   --relocations --expand-relocs %t.o \
; RUN:   | FileCheck %s --check-prefix=ELF
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
; RUN:   | FileCheck %s --check-prefix=OBJ --implicit-check-not='<unknown>'

target triple = "mmix-unknown-elf"

%small = type <{ i8, i32 }>
%large = type { i64, i64 }

@aggregate_source = global %large { i64 17, i64 23 }, align 8
@aggregate_result = global %large zeroinitializer, align 8
@external_data = external global i64
@external_data_pointer = global ptr getelementptr (i8, ptr @external_data,
                                                    i64 -8), align 8
@external_function_pointer = global ptr @external_scalar, align 8

declare i64 @external_scalar(i64)
declare %small @external_direct(%small, ptr byval(%large) align 8)
declare void @external_sret(ptr sret(%large) align 8, i64)
declare i64 @external_variadic(i64, ...)
declare void @external_addend()
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)

define internal i64 @local_scalar(i64 %value) {
  %result = add i64 %value, 1
  ret i64 %result
}

; Compose ordinary scalar calls with both direct and caller-copy aggregates.
; ASM-LABEL: call_values:
; ASM:       PUSHJB r31, local_scalar
; ASM:       GETA {{r[0-9]+}}, %geta(external_scalar)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ASM:       GETA {{r[0-9]+}}, %geta(external_direct)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
define %small @call_values(i64 %value, %small %direct,
                           ptr byval(%large) align 8 %copy) {
  %local = call i64 @local_scalar(i64 %value)
  %external = call i64 @external_scalar(i64 %local)
  %result = call %small @external_direct(
      %small %direct, ptr byval(%large) align 8 %copy)
  %sink = add i64 %external, 1
  store volatile i64 %sink, ptr @external_data, align 8
  ret %small %result
}

; The hidden result pointer and a variadic caller-copy operand coexist with
; ordinary slots. Global addresses retain canonical relocatable expressions.
; ASM-LABEL: call_results_and_varargs:
; ASM:       GETA {{r[0-9]+}}, %geta(aggregate_result)
; ASM:       GETA {{r[0-9]+}}, %geta(external_sret)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ASM:       GETA {{r[0-9]+}}, %geta(aggregate_source)
; ASM:       GETA {{r[0-9]+}}, %geta(external_variadic)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
define i64 @call_results_and_varargs(%small %direct) {
  call void @external_sret(
      ptr sret(%large) align 8 @aggregate_result, i64 29)
  %result = call i64 (i64, ...) @external_variadic(
      i64 31, %small %direct,
      ptr byval(%large) align 8 @aggregate_source)
  ret i64 %result
}

; A large intrinsic copy proves that an ABI object may retain an ordinary C
; runtime-helper reference without claiming that the runtime is available.
; ASM-LABEL: copy_object:
; ASM:       GETA {{r[0-9]+}}, %geta(memcpy)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
define void @copy_object(ptr %destination, ptr %source) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %destination,
                                   ptr align 8 %source, i64 128, i1 false)
  ret void
}

; Symbolic calls retain one signed S+A expression for the object writer.
; ASM-LABEL: call_signed_addend:
; ASM:       GETA {{r[0-9]+}}, %geta(external_addend-12)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
define void @call_signed_addend() {
  call void getelementptr (i8, ptr @external_addend, i64 -12)()
  ret void
}

; Register-indirect calls use PUSHGO directly and have no call-site symbol.
; ASM-LABEL: call_indirect:
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
define i64 @call_indirect(ptr %callee, i64 %value) nounwind {
  %result = call i64 %callee(i64 %value)
  ret i64 %result
}

; ELF:      Name: .text
; ELF:      Type: SHT_PROGBITS
; ELF:      Name: .rela.text
; ELF:      Type: SHT_RELA
; ELF:      Name: .data
; ELF:      Type: SHT_PROGBITS
; ELF:      SectionData (
; ELF:      0000: 00000000 00000011 00000000 00000017
; ELF:      Name: .rela.data
; ELF:      Type: SHT_RELA
; ELF:      Name: .bss
; ELF:      Type: SHT_NOBITS

; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_scalar
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_direct
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_data
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: aggregate_result
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_sret
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: aggregate_source
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_variadic
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: memcpy
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_addend
; ELF-NEXT: Addend: 0xFFFFFFFFFFFFFFF4
; ELF:      Type: R_MMIX_64 (5)
; ELF-NEXT: Symbol: external_data
; ELF-NEXT: Addend: 0xFFFFFFFFFFFFFFF8
; ELF:      Type: R_MMIX_64 (5)
; ELF-NEXT: Symbol: external_scalar

; ELF:      Name: local_scalar
; ELF:      Binding: Local
; ELF-NEXT: Type: Function
; ELF:      Name: external_scalar
; ELF:      Section: Undefined
; ELF:      Name: external_data
; ELF:      Section: Undefined
; ELF:      Name: aggregate_result
; ELF:      Type: Object
; ELF:      Name: aggregate_source
; ELF:      Type: Object
; ELF:      Name: memcpy
; ELF:      Section: Undefined

; Same-section local calls resolve without a relocation, while external calls
; use stubbable direct-call records.
; OBJ-LABEL: <call_values>:
; OBJ:       PUSHJB r31,
; OBJ:       GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA external_scalar
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
; OBJ:       GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA external_direct
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0

; The GETA relocation denotes global storage in object output.
; OBJ-LABEL: <call_results_and_varargs>:
; OBJ:       GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA aggregate_result
; OBJ:       GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA external_sret
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
; OBJ:       GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA aggregate_source
; OBJ:       GETA {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} R_MMIX_GETA external_variadic
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0

; OBJ-LABEL: <call_indirect>:
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
; OBJ-NEXT:  {{.*}} PUT rJ,
; OBJ-NEXT:  {{.*}} POP 0, 0
