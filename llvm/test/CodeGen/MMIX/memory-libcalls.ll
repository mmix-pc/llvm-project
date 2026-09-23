; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
; RUN:   -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=asm \
; RUN:   %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
; RUN:   %s -o %t.o
; RUN: llvm-readobj --relocations --expand-relocs %t.o \
; RUN:   | FileCheck %s --check-prefix=RELOCS --implicit-check-not=R_MMIX_
; RUN: llvm-readobj --symbols %t.o | FileCheck %s --check-prefix=SYMBOLS

target triple = "mmix-unknown-elf"

; Small aligned copies use exact-width loads and stores without producing an
; external helper reference.
; ASM-LABEL: copy_16:
; ASM:       LDOU [[HIGH:r[0-9]+]], r232, 8
; ASM-NEXT:  STOU [[HIGH]], r231, 8
; ASM-NEXT:  LDOU [[LOW:r[0-9]+]], r232, 0
; ASM-NEXT:  STOU [[LOW]], r231, 0
; ASM-NOT:   memcpy
define void @copy_16(ptr %destination, ptr %source) nounwind {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %destination,
                                   ptr align 8 %source, i64 16, i1 false)
  ret void
}

; Insufficient alignment forces byte operations, preserving all three bytes
; without relying on MMIX's address-rounding multi-byte accesses.
; ASM-LABEL: copy_unaligned_3:
; ASM:       LDBU [[BYTE2:r[0-9]+]], r232, 2
; ASM-NEXT:  STBU [[BYTE2]], r231, 2
; ASM-NEXT:  LDBU [[BYTE1:r[0-9]+]], r232, 1
; ASM-NEXT:  STBU [[BYTE1]], r231, 1
; ASM-NEXT:  LDBU [[BYTE0:r[0-9]+]], r232, 0
; ASM-NEXT:  STBU [[BYTE0]], r231, 0
; ASM-NOT:   memcpy
define void @copy_unaligned_3(ptr %destination, ptr %source) nounwind {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %destination,
                                   ptr align 1 %source, i64 3, i1 false)
  ret void
}

; An inline memmove loads every source octa before storing either destination
; octa, so overlap cannot overwrite a value that is still needed.
; ASM-LABEL: move_16:
; ASM:       LDOU [[FIRST:r[0-9]+]], r232, 0
; ASM-NEXT:  LDOU [[SECOND:r[0-9]+]], r232, 8
; ASM-NEXT:  STOU [[SECOND]], r231, 8
; ASM-NEXT:  STOU [[FIRST]], r231, 0
; ASM-NOT:   memmove
define void @move_16(ptr %destination, ptr %source) nounwind {
  call void @llvm.memmove.p0.p0.i64(ptr align 8 %destination,
                                    ptr align 8 %source, i64 16, i1 false)
  ret void
}

; Small fills may synthesize a repeated byte and store complete aligned octas.
; ASM-LABEL: set_16:
; ASM-COUNT-2: STOU
; ASM-NOT: memset
define void @set_16(ptr %destination, i32 signext %value) nounwind {
  %byte = trunc i32 %value to i8
  call void @llvm.memset.p0.i64(ptr align 8 %destination, i8 %byte, i64 16,
                                i1 false)
  ret void
}

; Five aligned stores remain profitable normally but exceed the four-store
; size-optimized threshold.
; ASM-LABEL: copy_40:
; ASM:       LDOU [[OCTA4:r[0-9]+]], r232, 32
; ASM-NEXT:  STOU [[OCTA4]], r231, 32
; ASM-NEXT:  LDOU [[OCTA3:r[0-9]+]], r232, 24
; ASM-NEXT:  STOU [[OCTA3]], r231, 24
; ASM-NEXT:  LDOU [[OCTA2:r[0-9]+]], r232, 16
; ASM-NEXT:  STOU [[OCTA2]], r231, 16
; ASM-NEXT:  LDOU [[OCTA1:r[0-9]+]], r232, 8
; ASM-NEXT:  STOU [[OCTA1]], r231, 8
; ASM-NEXT:  LDOU [[OCTA0:r[0-9]+]], r232, 0
; ASM-NEXT:  STOU [[OCTA0]], r231, 0
; ASM-NOT:   memcpy
define void @copy_40(ptr %destination, ptr %source) nounwind {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %destination,
                                   ptr align 8 %source, i64 40, i1 false)
  ret void
}

; ASM-LABEL: copy_40_minsize:
; ASM:       GETA {{r[0-9]+}}, %geta(memcpy)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: copy_40_minsize
; ISEL:       DIRECT_CALL_STATE &memcpy, {{.*}}implicit $r231, implicit $r232, implicit $r233
define void @copy_40_minsize(ptr %destination, ptr %source) nounwind minsize {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %destination,
                                   ptr align 8 %source, i64 40, i1 false)
  ret void
}

; Large and dynamic copies use the standard memcpy pointer, pointer, size_t
; signature in the first three ordinary ABI slots.
; ASM-LABEL: copy_128:
; ASM:       GETA {{r[0-9]+}}, %geta(memcpy)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: copy_128
; ISEL:       DIRECT_CALL_STATE &memcpy, {{.*}}implicit $r231, implicit $r232, implicit $r233
define void @copy_128(ptr %destination, ptr %source) nounwind {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %destination,
                                   ptr align 8 %source, i64 128, i1 false)
  ret void
}

; ASM-LABEL: copy_dynamic:
; ASM:       GETA {{r[0-9]+}}, %geta(memcpy)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: copy_dynamic
; ISEL:       DIRECT_CALL_STATE &memcpy, {{.*}}implicit $r231, implicit $r232, implicit $r233
define void @copy_dynamic(ptr %destination, ptr %source, i64 %size) nounwind {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %destination,
                                   ptr align 1 %source, i64 %size, i1 false)
  ret void
}

; Large moves and fills use only the other two standard C memory helpers.
; ASM-LABEL: move_128:
; ASM:       GETA {{r[0-9]+}}, %geta(memmove)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: move_128
; ISEL:       DIRECT_CALL_STATE &memmove, {{.*}}implicit $r231, implicit $r232, implicit $r233
define void @move_128(ptr %destination, ptr %source) nounwind {
  call void @llvm.memmove.p0.p0.i64(ptr align 8 %destination,
                                    ptr align 8 %source, i64 128, i1 false)
  ret void
}

; ASM-LABEL: set_128:
; ASM:       GETA {{r[0-9]+}}, %geta(memset)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: set_128
; ISEL:       DIRECT_CALL_STATE &memset, {{.*}}implicit $r231, implicit $r232, implicit $r233
define void @set_128(ptr %destination, i32 signext %value) nounwind {
  %byte = trunc i32 %value to i8
  call void @llvm.memset.p0.i64(ptr align 8 %destination, i8 %byte, i64 128,
                                i1 false)
  ret void
}

; CTPOP has a native implementation and must not acquire an ffs helper call.
; ASM-LABEL: population_count:
; ASM:       SADD r231, r231, 0
; ASM-NOT:   __ffsdi2
define i64 @population_count(i64 %value) nounwind {
  %count = call i64 @llvm.ctpop.i64(i64 %value)
  ret i64 %count
}

; GNU __ffsdi2 compatibility is represented by an explicit ordinary C call.
; Producing this undefined reference does not claim a runtime implementation.
; ASM-LABEL: explicit_ffsdi2:
; ASM:       GETA {{r[0-9]+}}, %geta(__ffsdi2)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: explicit_ffsdi2
; ISEL:       DIRECT_CALL_STATE @__ffsdi2, {{.*}}implicit $r231, implicit-def $r254, implicit-def $r231
define i32 @explicit_ffsdi2(i64 %value) nounwind {
  %result = call i32 @__ffsdi2(i64 %value)
  ret i32 %result
}

; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: memcpy
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: memcpy
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: memcpy
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: memmove
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: memset
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: __ffsdi2

; SYMBOLS:      Name: memcpy
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: memmove
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: memset
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: __ffsdi2
; SYMBOLS:      Section: Undefined

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
declare i64 @llvm.ctpop.i64(i64)
declare i32 @__ffsdi2(i64)
