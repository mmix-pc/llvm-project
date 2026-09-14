; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-unknown -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -stop-after=atomic-expand %s -o - | FileCheck %s --check-prefix=IR

define ptr @load_pointer(ptr %address) {
; IR-LABEL: define ptr @load_pointer(
; IR: cmpxchg ptr %address, i64 0, i64 0 monotonic monotonic, align 8
; IR: [[LOADED:%.*]] = extractvalue { i64, i1 }
; IR: fence acquire
; IR: inttoptr i64 [[LOADED]] to ptr
; CHECK-LABEL: load_pointer:
; CHECK: CSWAP
; CHECK: POP
  %value = load atomic ptr, ptr %address acquire, align 8
  ret ptr %value
}

define void @store_pointer(ptr %address, ptr %value) {
; IR-LABEL: define void @store_pointer(
; IR: [[VALUE:%.*]] = ptrtoint ptr %value to i64
; IR: fence release
; IR: cmpxchg ptr %address, i64 {{%.*}}, i64 [[VALUE]] monotonic monotonic, align 8
; CHECK-LABEL: store_pointer:
; CHECK: CSWAP
; CHECK: POP
  store atomic ptr %value, ptr %address release, align 8
  ret void
}

define ptr @exchange_pointer(ptr %address, ptr %value) {
; CHECK-LABEL: exchange_pointer:
; CHECK: CSWAP
; CHECK: POP
  %old = atomicrmw xchg ptr %address, ptr %value seq_cst
  ret ptr %old
}

define ptr @compare_pointer(ptr %address, ptr %expected, ptr %value) {
; CHECK-LABEL: compare_pointer:
; CHECK: CSWAP
; CHECK: POP
  %result = cmpxchg ptr %address, ptr %expected, ptr %value acq_rel acquire
  %old = extractvalue { ptr, i1 } %result, 0
  ret ptr %old
}
