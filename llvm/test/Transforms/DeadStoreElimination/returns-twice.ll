; RUN: opt -passes=dse -verify-memoryssa -S %s | FileCheck %s

target datalayout = "e-p:64:64:64"

declare noalias ptr @malloc(i64) allockind("alloc,uninitialized")
declare void @free(ptr captures(none)) nounwind allockind("free")
declare i32 @checkpoint(ptr) returns_twice nounwind
declare i32 @other_checkpoint(ptr) nounwind
declare void @jump(ptr) noreturn nounwind
declare void @maybe_jump(ptr) nounwind
declare void @memory_free_jump() memory(none) nounwind
declare void @ordinary() nounwind willreturn

; A longjmp wrapper cannot directly read this allocation, but it can resume a
; read in the same function. Neither nounwind nor noreturn excludes that edge.
define i32 @heap_before_jump(ptr %env) {
; CHECK-LABEL: @heap_before_jump(
; CHECK: store i32 17, ptr %p
; CHECK: call i32 @checkpoint
; CHECK: store i32 43, ptr %p
; CHECK-NEXT: call void @jump
; CHECK: load i32, ptr %p
  %p = call ptr @malloc(i64 4)
  store i32 17, ptr %p
  %r = call i32 @checkpoint(ptr %env)
  %first = icmp eq i32 %r, 0
  br i1 %first, label %initial, label %resumed
initial:
  store i32 43, ptr %p
  call void @jump(ptr %env)
  unreachable
resumed:
  %value = load i32, ptr %p
  call void @free(ptr %p)
  ret i32 %value
}

; Indirect calls and call-site returns_twice attributes have the same issue.
define i32 @indirect_jump(ptr %env, ptr %fn) {
; CHECK-LABEL: @indirect_jump(
; CHECK: call i32 @other_checkpoint
; CHECK: store i32 43, ptr %p
; CHECK-NEXT: call void %fn
  %p = call ptr @malloc(i64 4)
  %r = call i32 @other_checkpoint(ptr %env) returns_twice
  %first = icmp eq i32 %r, 0
  br i1 %first, label %initial, label %resumed
initial:
  store i32 43, ptr %p
  call void %fn(ptr %env) noreturn nounwind
  unreachable
resumed:
  %value = load i32, ptr %p
  ret i32 %value
}

; A potentially returning call can bypass an overwrite or a free, even when
; the killer postdominates the store in the explicit CFG.
define i32 @bypassed_overwrite(ptr %env) {
; CHECK-LABEL: @bypassed_overwrite(
; CHECK: store i32 43, ptr %p
; CHECK-NEXT: call void @maybe_jump
; CHECK-NEXT: store i32 99, ptr %p
  %p = call ptr @malloc(i64 4)
  %r = call i32 @checkpoint(ptr %env)
  %first = icmp eq i32 %r, 0
  br i1 %first, label %initial, label %resumed
initial:
  store i32 43, ptr %p
  call void @maybe_jump(ptr %env)
  store i32 99, ptr %p
  br label %resumed
resumed:
  %value = load i32, ptr %p
  ret i32 %value
}

define i32 @bypassed_free(ptr %env) {
; CHECK-LABEL: @bypassed_free(
; CHECK: store i32 43, ptr %p
; CHECK-NEXT: call void @maybe_jump
; CHECK-NEXT: call void @free
  %p = call ptr @malloc(i64 4)
  %r = call i32 @checkpoint(ptr %env)
  %first = icmp eq i32 %r, 0
  br i1 %first, label %initial, label %resumed
initial:
  store i32 43, ptr %p
  call void @maybe_jump(ptr %env)
  call void @free(ptr %p)
  ret i32 0
resumed:
  %value = load i32, ptr %p
  ret i32 %value
}

; A MemorySSA-only call barrier would miss this call. Restricted memory
; effects do not by themselves promise normal return to the call site.
define i32 @memory_free_nonlocal_return(ptr %env) {
; CHECK-LABEL: @memory_free_nonlocal_return(
; CHECK: store i32 43, ptr %p
; CHECK-NEXT: call void @memory_free_jump
; CHECK-NEXT: call void @free
  %p = call ptr @malloc(i64 4)
  %r = call i32 @checkpoint(ptr %env)
  %first = icmp eq i32 %r, 0
  br i1 %first, label %initial, label %resumed
initial:
  store i32 43, ptr %p
  call void @memory_free_jump()
  call void @free(ptr %p)
  ret i32 0
resumed:
  %value = load i32, ptr %p
  ret i32 %value
}

; The unreachable exit cannot be ignored when proving complete overwrite.
define i32 @bypassed_branch(ptr %env, i1 %jumping) {
; CHECK-LABEL: @bypassed_branch(
; CHECK: store i32 43, ptr %p
; CHECK: call void @jump
; CHECK: store i32 99, ptr %p
  %p = call ptr @malloc(i64 4)
  %r = call i32 @checkpoint(ptr %env)
  %first = icmp eq i32 %r, 0
  br i1 %first, label %initial, label %resumed
initial:
  store i32 43, ptr %p
  br i1 %jumping, label %nonlocal, label %overwrite
nonlocal:
  call void @jump(ptr %env)
  unreachable
overwrite:
  store i32 99, ptr %p
  br label %resumed
resumed:
  %value = load i32, ptr %p
  ret i32 %value
}

; Functions without nonlocal resumption still get ordinary DSE.
define void @ordinary_overwrite(ptr %p) {
; CHECK-LABEL: @ordinary_overwrite(
; CHECK-NOT: store i32 43
; CHECK: store i32 99, ptr %p
  store i32 43, ptr %p
  store i32 99, ptr %p
  ret void
}

define void @ordinary_heap() {
; CHECK-LABEL: @ordinary_heap(
; CHECK-NOT: store
; CHECK: ret void
  %p = call ptr @malloc(i64 4)
  store i32 43, ptr %p
  call void @ordinary()
  call void @free(ptr %p)
  ret void
}
