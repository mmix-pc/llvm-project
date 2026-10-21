; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs -stop-after=prolog-epilog %s -o - | FileCheck %s --check-prefix=PEI
; RUN: llc -mtriple=mmix -O2 -verify-machineinstrs -filetype=obj %s -o %t.o

target triple = "mmix"

declare ptr @llvm.stacksave()
declare void @llvm.stackrestore(ptr)
declare i64 @consume17(i64, i64, i64, i64, i64, i64, i64, i64, i64,
                       i64, i64, i64, i64, i64, i64, i64, i64)

; ASM-LABEL: dynamic_alloca:
; ASM:       STOU r253
; ASM:       ADDU r253, r254
; ASM:       SUBU [[OBJECT:r[0-9]+]], {{r[0-9]+}}, {{r[0-9]+}}
; ASM-NEXT:  OR r254, [[OBJECT]], 0
; ASM:       SUBU r254, r253,
; ASM-NEXT:  NEGU r255, 0, 8
; ASM-NEXT:  LDOU r253, r253, r255
define i64 @dynamic_alloca(i64 %count) nounwind {
  %storage = alloca i64, i64 %count, align 8
  store volatile i64 1, ptr %storage
  %value = load volatile i64, ptr %storage
  ret i64 %value
}

; A fixed local remains frame-pointer-relative while the dynamic frontier is
; saved, moved twice, and restored twice.
; PEI-LABEL: name: nested_restore
; PEI:       stackSize: 16
; PEI:       $r253 = frame-setup ADDUI $r254, 16
; PEI:       [[OUTER:\$r[0-9]+]] = COPY $r254
; PEI:       $r254 = COPY
; PEI:       [[INNER:\$r[0-9]+]] = COPY $r254
; PEI:       $r254 = COPY
; PEI:       $r254 = COPY {{(killed )?}}[[INNER]]
; PEI:       $r254 = COPY {{(killed )?}}[[OUTER]]
; PEI:       $r254 = frame-destroy SUBUI $r253, 16
; PEI:       $r253 = LDOU $r253,
; PEI:       $r254 = frame-destroy ADDUI $r254, 16
define i64 @nested_restore(i64 %outer_count, i64 %inner_count) {
  %fixed = alloca i64, align 8
  store volatile i64 11, ptr %fixed
  %outer_save = call ptr @llvm.stacksave()
  %outer = alloca i8, i64 %outer_count, align 1
  store volatile i8 22, ptr %outer
  %inner_save = call ptr @llvm.stacksave()
  %inner = alloca i32, i64 %inner_count, align 4
  store volatile i32 33, ptr %inner
  call void @llvm.stackrestore(ptr %inner_save)
  %outer_value = load volatile i8, ptr %outer
  call void @llvm.stackrestore(ptr %outer_save)
  %fixed_value = load volatile i64, ptr %fixed
  %outer_wide = zext i8 %outer_value to i64
  %result = add i64 %fixed_value, %outer_wide
  ret i64 %result
}

; A dynamic function allocates its outgoing stack slot below live dynamic
; objects and restores the pre-call frontier after the call.
; ASM-LABEL: dynamic_call:
; ASM:       SUBU {{r[0-9]+}}, {{r[0-9]+}}, {{r[0-9]+}}
; ASM:       OR r254, {{r[0-9]+}}, 0
; ASM:       SUBU r254, r254, 8
; ASM:       PUSHGO
; ASM:       ADDU r254, r254, 8
; ASM:       LDOU {{r[0-9]+}}, {{r[0-9]+}}, 0
; ASM:       SUBU r254, r253,
; PEI-LABEL: name: dynamic_call
; PEI:       maxCallFrameSize: 8
; PEI:       $r254 = SUBUI $r254, 8
; PEI:       CALL_STATE
; PEI:       $r254 = ADDUI $r254, 8
define i64 @dynamic_call(i64 %count) {
  %storage = alloca i64, i64 %count, align 8
  store volatile i64 44, ptr %storage
  %call = call i64 @consume17(
      i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7,
      i64 8, i64 9, i64 10, i64 11, i64 12, i64 13, i64 14, i64 15,
      i64 16)
  %live = load volatile i64, ptr %storage
  %result = add i64 %call, %live
  ret i64 %result
}
