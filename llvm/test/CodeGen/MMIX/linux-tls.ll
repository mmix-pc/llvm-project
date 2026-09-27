; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -stop-after=finalize-isel < %s | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=mmix-unknown-linux -O0 -filetype=obj < %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
; RUN: llc -mtriple=mmix-unknown-linux -O2 -filetype=obj < %s -o %t.o2.o
; RUN: llvm-readobj -r %t.o2.o | FileCheck %s --check-prefix=RELOC

@local = internal thread_local global i64 42
@external = external thread_local(localexec) global i64
@zero = thread_local(localexec) global i64 0
@refined = dso_local thread_local(localdynamic) global i64 7
@local_external = external dso_local thread_local global i64

; A default-model local definition refines to local-exec. Never form the
; template's ordinary address or discard the reserved TP register dependency.
define ptr @address() {
; CHECK-LABEL: address:
; CHECK: SETL [[R:r[0-9]+]], %tprel_lo(local)
; CHECK-NEXT: INCML [[R]], %tprel_ml(local)
; CHECK-NEXT: INCMH [[R]], %tprel_mh(local)
; CHECK-NEXT: INCH [[R]], %tprel_hi(local)
; CHECK-NEXT: ADDU [[R]], r230, [[R]]
; MIR: LOAD_TLS_ADDR @local, implicit $r230
; RELOC: R_MMIX_TPREL_LO16 local 0x0
; RELOC-NEXT: R_MMIX_TPREL_ML16 local 0x0
; RELOC-NEXT: R_MMIX_TPREL_MH16 local 0x0
; RELOC-NEXT: R_MMIX_TPREL_HI16 local 0x0
  ret ptr @local
}

define i64 @load_external() {
; CHECK-LABEL: load_external:
; CHECK: %tprel_lo(external)
; CHECK: ADDU [[E:r[0-9]+]], r230, [[E]]
; CHECK: LDO{{U?}} {{r[0-9]+}}, [[E]], 0
  %v = load i64, ptr @external
  ret i64 %v
}

define void @store_zero(i64 %v) {
; CHECK-LABEL: store_zero:
; CHECK: %tprel_lo(zero)
; CHECK: ADDU [[Z:r[0-9]+]], r230, [[Z]]
; CHECK: STO{{U?}} {{r[0-9]+}}, [[Z]], 0
  store i64 %v, ptr @zero
  ret void
}

define ptr @offset() {
; CHECK-LABEL: offset:
; CHECK: %tprel_lo(local
; CHECK: ADDU {{r[0-9]+}}, r230,
; CHECK: SUBU {{r[0-9]+}}, {{r[0-9]+}}, 8
  %p = getelementptr i8, ptr @local, i64 -8
  ret ptr %p
}

declare ptr @llvm.thread.pointer()
declare ptr @llvm.threadlocal.address.p0(ptr)

define ptr @read_tp() {
; CHECK-LABEL: read_tp:
; CHECK: {{OR|ADDU}} r231, r230, 0
  %p = call ptr @llvm.thread.pointer()
  ret ptr %p
}

define ptr @intrinsic_address() {
; CHECK-LABEL: intrinsic_address:
; CHECK: %tprel_lo(external)
; CHECK: ADDU {{r[0-9]+}}, r230,
  %p = call ptr @llvm.threadlocal.address.p0(ptr @external)
  ret ptr %p
}

declare void @ordinary()
declare fastcc void @fast()

define ptr @across_calls() {
; CHECK-LABEL: across_calls:
; CHECK: PUSHGO
; CHECK: PUSHGO
; CHECK: {{OR|ADDU}} r231, r230, 0
; MIR-LABEL: name: across_calls
; MIR: csr_mmix_linux
; MIR: csr_mmix_linux
; MIR: COPY $r230
  call void @ordinary()
  call fastcc void @fast()
  %p = call ptr @llvm.thread.pointer()
  ret ptr %p
}

define ptr @refined_model() {
; CHECK-LABEL: refined_model:
; CHECK: %tprel_lo(refined)
; CHECK: ADDU {{r[0-9]+}}, r230,
  ret ptr @refined
}

define ptr @refined_declaration() {
; CHECK-LABEL: refined_declaration:
; CHECK: %tprel_lo(local_external)
; CHECK: ADDU {{r[0-9]+}}, r230,
  ret ptr @local_external
}

; Force live TLS values out of all ordinary preserved local registers to
; exercise spills across C/fastcc calls, then use TLS again after both calls.
define fastcc i64 @pressure() {
; CHECK-LABEL: pressure:
; CHECK: %tprel_lo(local)
; CHECK: ADDU {{r[0-9]+}}, r230,
; CHECK: STOU
; CHECK: PUSHGO
; CHECK: PUSHGO
; CHECK: LDOU
; CHECK: POP
  %a = load volatile i64, ptr @local
  %b = load volatile i64, ptr @zero
  %c = load volatile i64, ptr @external
  %d = load volatile i64, ptr @refined
  call void asm sideeffect "", "~{r0},~{r1},~{r2},~{r3},~{r4},~{r5},~{r6},~{r7},~{r8},~{r9},~{r10},~{r11},~{r12},~{r13},~{r14},~{r15},~{r16},~{r17},~{r18},~{r19},~{r20},~{r21},~{r22},~{r23},~{r24},~{r25},~{r26},~{r27},~{r28},~{r29},~{r231},~{r232},~{r233},~{r234},~{r235},~{r236},~{r237},~{r238},~{r239},~{r240},~{r241},~{r242},~{r243},~{r244},~{r245},~{r246},~{r247},~{r248},~{r249},~{r250}"()
  call void @ordinary()
  call fastcc void @fast()
  %ab = add i64 %a, %b
  %cd = add i64 %c, %d
  %sum = add i64 %ab, %cd
  store volatile i64 %sum, ptr @local
  ret i64 %sum
}
