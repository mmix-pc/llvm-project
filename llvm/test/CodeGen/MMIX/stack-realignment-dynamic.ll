; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix -filetype=obj %s -o %t.o
; RUN: llvm-dwarfdump --verify %t.o
; RUN: llvm-dwarfdump --eh-frame %t.o | FileCheck %s --check-prefix=CFI
; RUN: llvm-dwarfdump --debug-info %t.o | FileCheck %s --check-prefix=DEBUG

; CFI: DW_CFA_offset: RJ -16
; CFI: DW_CFA_offset: R253 -8
; CFI: DW_CFA_def_cfa: R253 +0
; DEBUG: DW_AT_location (DW_OP_bregx R29+{{[0-9]+}})
; DEBUG-NEXT: DW_AT_name ("fixed_local")

target triple = "mmix"
declare ptr @llvm.stacksave.p0()
declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
declare void @llvm.stackrestore.p0(ptr)
declare i32 @__gxx_personality_v0(...)
declare void @inspect_frame(ptr, ptr, i64, i64)
declare void @inspect_cleanup(ptr, ptr, i64) nounwind
declare i64 @stack_callee(i64, i64, i64, i64, i64, i64, i64, i64,
                         i64, i64, i64, i64, i64, i64, i64, i64, i64, i64)

; CHECK-LABEL: realigned_dynamic:
; CHECK: .cfi_offset rJ, -16
; CHECK: .cfi_offset r253, -8
; CHECK: .cfi_def_cfa r253, 0
; CHECK: ANDN r254, r254, 63
; CHECK-NEXT: OR r29, r254, 0
; CHECK: STOU {{.*}}, r29,
; CHECK: PUSHGO
; CHECK: SUBU r254, r253,
; CHECK: .cfi_def_cfa r254,
; CHECK: LDOU r253,
; CHECK: .cfi_same_value r253
; CHECK: ADDU r254, r254,
; CHECK: .cfi_def_cfa_offset 0
; CHECK: .cfi_restore_state
; CHECK: %geta(inspect_cleanup)
define i64 @realigned_dynamic(i64 %n, i64 %throws) uwtable(sync)
    personality ptr @__gxx_personality_v0 !dbg !5 {
entry:
  %fixed = alloca [64 x i8], align 64
  #dbg_declare(ptr %fixed, !9, !DIExpression(), !10)
  store volatile i64 12345, ptr %fixed, align 64, !dbg !10
  %saved = call ptr @llvm.stacksave.p0()
  %dynamic = alloca i64, i64 %n, align 8
  store volatile i64 54321, ptr %dynamic, align 8
  %result = call i64 @stack_callee(i64 0, i64 1, i64 2, i64 3,
      i64 4, i64 5, i64 6, i64 7, i64 8, i64 9, i64 10, i64 11,
      i64 12, i64 13, i64 14, i64 15, i64 16, i64 17), !dbg !11
  invoke void @inspect_frame(ptr %fixed, ptr %dynamic, i64 %n, i64 %throws)
      to label %normal unwind label %cleanup, !dbg !10
normal:
  call void @llvm.stackrestore.p0(ptr %saved)
  %value = load volatile i64, ptr %fixed, align 64, !dbg !10
  %sum = add i64 %value, %result
  ret i64 %sum, !dbg !10
cleanup:
  %lp = landingpad { ptr, i32 } cleanup
  call void @inspect_cleanup(ptr %fixed, ptr %dynamic, i64 %n), !dbg !10
  resume { ptr, i32 } %lp
}

; CHECK-LABEL: realigned_wrapper:
; CHECK: .cfi_def_cfa r253, 0
; CHECK: ANDN r254, r254, 31
; CHECK: PUSHGO
; CHECK: SUBU r254, r253,
define i64 @realigned_wrapper(i64 %n, i64 %throws) uwtable(sync) {
  %fixed = alloca i64, align 32
  store volatile i64 7, ptr %fixed, align 32
  %r = call i64 @realigned_dynamic(i64 %n, i64 %throws)
  %v = load volatile i64, ptr %fixed, align 32
  %sum = add i64 %r, %v
  ret i64 %sum
}

; The fifteen unnamed register slots occupy CFA-120 through CFA-8. Neither
; the saved return address nor the old FP may overlap that area.
; CHECK-LABEL: realigned_variadic:
; CHECK: .cfi_offset rJ, -136
; CHECK: .cfi_offset r253, -128
; CHECK: ANDN r254, r254, 31
; CHECK: PUSHGO
define i64 @realigned_variadic(i64 %n, ...) uwtable(sync) {
  %ap = alloca ptr, align 32
  call void @llvm.va_start(ptr %ap)
  %r = call i64 @realigned_wrapper(i64 %n, i64 0)
  %cursor = load ptr, ptr %ap
  %first = load i64, ptr %cursor
  %last = getelementptr i64, ptr %cursor, i64 15
  %stack = load i64, ptr %last
  call void @llvm.va_end(ptr %ap)
  %s = add i64 %r, %first
  %result = add i64 %s, %stack
  ret i64 %result
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}
!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "MMIX realignment test", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "stack-realignment.c", directory: "/src")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!5 = distinct !DISubprogram(name: "realigned_dynamic", scope: !1, file: !1, line: 1, type: !6, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!6 = !DISubroutineType(types: !7)
!7 = !{!8, !8, !8}
!8 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!9 = !DILocalVariable(name: "fixed_local", scope: !5, file: !1, line: 2, type: !8)
!10 = !DILocation(line: 3, column: 1, scope: !5)
!11 = !DILocation(line: 4, column: 1, scope: !5)
