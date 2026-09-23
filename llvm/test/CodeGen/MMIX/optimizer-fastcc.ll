; RUN: opt -mtriple=mmix -passes=globalopt -S %s -o %t.ll
; RUN: FileCheck %s --check-prefix=OPT < %t.ll
; RUN: llc -mtriple=mmix -filetype=obj %t.ll -o %t.o
; RUN: llvm-readobj --file-headers --symbols --relocations %t.o \
; RUN:   | FileCheck %s --check-prefix=OBJ
; RUN: opt -mtriple=mmix -passes='default<O0>' -S %s \
; RUN:   | FileCheck %s --check-prefix=O0

target triple = "mmix-unknown-elf"

@saved_address = global ptr @address_taken

declare i64 @external_callee(i64)
declare i64 @__compiler_runtime_helper(i64)

define i64 @public_caller(i64 %value) {
; OPT-LABEL: define i64 @public_caller(
; OPT:         %internal = call fastcc i64 @internal_callee(i64 %value)
; OPT:         %external = call i64 @external_callee(i64 %internal)
; OPT:         %runtime = call i64 @__compiler_runtime_helper(i64 %external)
; OPT:         %variadic = call i64 (i64, ...) @internal_variadic(i64 %runtime, i64 1)
; OPT:         ret i64 %variadic
;
; O0-LABEL: define i64 @public_caller(
; O0:         %internal = call i64 @internal_callee(i64 %value)
; O0-NOT:     fastcc
;
  %internal = call i64 @internal_callee(i64 %value)
  %external = call i64 @external_callee(i64 %internal)
  %runtime = call i64 @__compiler_runtime_helper(i64 %external)
  %variadic = call i64 (i64, ...) @internal_variadic(i64 %runtime, i64 1)
  ret i64 %variadic
}

define internal i64 @internal_callee(i64 %value) noinline {
; OPT-LABEL: define internal fastcc i64 @internal_callee(
; OPT:         ret i64
;
; O0-LABEL: define internal i64 @internal_callee(
; O0-NOT:     fastcc
;
  %result = add i64 %value, 3
  ret i64 %result
}

define internal i64 @address_taken(i64 %value) {
; OPT-LABEL: define internal i64 @address_taken(
; OPT-NOT:     fastcc
;
  ret i64 %value
}

define internal i64 @internal_variadic(i64 %value, ...) {
; OPT-LABEL: define internal i64 @internal_variadic(
; OPT-NOT:     fastcc
;
  ret i64 %value
}

; OBJ-DAG: Format: elf64-mmix
; OBJ-DAG: Arch: mmix
; OBJ-DAG: Name: public_caller
; OBJ-DAG: Name: external_callee
; OBJ-DAG: Name: __compiler_runtime_helper
; OBJ-DAG: R_MMIX_GETA external_callee
; OBJ-DAG: R_MMIX_GETA __compiler_runtime_helper
