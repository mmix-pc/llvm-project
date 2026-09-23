; RUN: llc -mtriple=mmix -exception-model=dwarf -verify-machineinstrs %s -o %t.s
; RUN: llc -O0 -mtriple=mmix -exception-model=dwarf -verify-machineinstrs %s -o %t.o0.s
; RUN: llc -mtriple=mmix -exception-model=dwarf -stop-after=finalize-isel %s -o %t.mir
; RUN: llc -mtriple=mmix -exception-model=dwarf -filetype=obj %s -o %t.o
; RUN: FileCheck %s --check-prefixes=ASM,OPT < %t.s
; RUN: FileCheck %s --check-prefixes=ASM,O0 < %t.o0.s
; RUN: FileCheck %s --check-prefix=MIR < %t.mir
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=OBJ

; Explicit selection and the default model share the same backend transfers.
; ASM-LABEL: invoke_cleanup:
; ASM: .cfi_startproc
; ASM: .cfi_offset rJ,
; ASM: .cfi_remember_state
; ASM: GETA r250, %geta(callee)
; ASM-NEXT: PUSHGO r31, r250, 0
; ASM: SETL r231, 42
; ASM: .cfi_register rJ, r30
; ASM: .cfi_def_cfa_offset 0
; ASM: PUT rJ, r30
; ASM: POP 0, 0
; ASM: .cfi_restore_state
; OPT: OR [[EXN:r[0-9]+]], r231, 0
; O0: STOU r231, r254, [[SLOT:[0-9]+]]
; ASM: GETA r250, %geta(cleanup)
; ASM-NEXT: PUSHGO r31, r250, 0
; O0: LDOU r231, r254, [[SLOT]]
; ASM: GETA r250, %geta(_Unwind_Resume)
; OPT: OR r231, [[EXN]], 0
; ASM: PUSHGO r31, r250, 0
; ASM-NOT: POP
; ASM-NOT: PUT rJ
; ASM: .cfi_endproc

; MIR: call void @_Unwind_Resume(ptr %exn.obj) #[[NR:[0-9]+]]
; MIR-NEXT: unreachable
; MIR: attributes #[[NR]] = { noreturn }
; MIR-LABEL: name: invoke_cleanup
; MIR: bb.0.entry:
; MIR-NEXT: successors: %bb.1({{.*}}), %bb.2({{.*}})
; MIR: EH_LABEL
; MIR: DIRECT_CALL_STATE @callee, {{.*}}csr_mmix, implicit-def {{(dead )?}}$rj, implicit-def {{(dead )?}}$rl, implicit-def {{(dead )?}}$ro
; MIR: EH_LABEL
; MIR: bb.2.unwind (landing-pad):
; MIR-NEXT: liveins: $r231, $r232
; MIR: [[SEL:%[0-9]+]]:gpr64codegen = COPY killed $r232
; MIR: [[PTR:%[0-9]+]]:gpr64codegen = COPY killed $r231
; MIR: $r231 = COPY [[PTR]]
; MIR: $r232 = COPY [[SEL]]
; MIR: DIRECT_CALL_STATE @cleanup, {{.*}}csr_mmix
; MIR: $r231 = COPY [[PTR]]
; MIR: DIRECT_CALL_STATE @_Unwind_Resume, {{.*}}csr_mmix
; MIR-NOT: RET
; MIR: ...
; OBJ: R_MMIX_GETA callee
; OBJ: R_MMIX_GETA cleanup
; OBJ: R_MMIX_GETA _Unwind_Resume

declare void @callee()
declare void @cleanup(ptr, i32)
declare i32 @__gxx_personality_v0(...)

define i32 @invoke_cleanup() personality ptr @__gxx_personality_v0 {
entry:
  invoke void @callee() to label %normal unwind label %unwind
normal:
  ret i32 42
unwind:
  %lp = landingpad { ptr, i32 } cleanup
  %p = extractvalue { ptr, i32 } %lp, 0
  %s = extractvalue { ptr, i32 } %lp, 1
  call void @cleanup(ptr %p, i32 %s)
  resume { ptr, i32 } %lp
}

@typeinfo = external constant ptr
declare i32 @llvm.eh.typeid.for(ptr)

; Check the selector's i32 semantics and a nested exceptional cleanup call.
; ASM-LABEL: dispatch_selector:
; ASM: GETA r250, %geta(callee)
; ASM: PUSHGO r31, r250, 0
; ASM: CMP
; ASM: GETA r250, %geta(cleanup)
; ASM: PUSHGO r31, r250, 0
; ASM: GETA r250, %geta(_Unwind_Resume)
; ASM: PUSHGO r31, r250, 0
; ASM: .cfi_endproc
; MIR-LABEL: name: dispatch_selector
; MIR: bb.{{[0-9]+}}.unwind (landing-pad):
; MIR: liveins: $r231, $r232
; MIR: COPY killed $r232
; MIR: COPY killed $r231
; MIR: CMP
; MIR: bb.{{[0-9]+}}.matched:
; MIR-NEXT: successors: %bb.{{[0-9]+}}({{.*}}), %bb.{{[0-9]+}}({{.*}})
; MIR: EH_LABEL
; MIR: DIRECT_CALL_STATE @cleanup
; MIR: EH_LABEL
; MIR: bb.{{[0-9]+}}.nested (landing-pad):
; MIR: liveins: $r231
; MIR: COPY killed $r231
; MIR: DIRECT_CALL_STATE @_Unwind_Resume
define fastcc i32 @dispatch_selector() personality ptr @__gxx_personality_v0 {
entry:
  invoke void @callee() to label %normal unwind label %unwind
normal:
  ret i32 0
unwind:
  %lp = landingpad { ptr, i32 } cleanup catch ptr @typeinfo
  %p = extractvalue { ptr, i32 } %lp, 0
  %s = extractvalue { ptr, i32 } %lp, 1
  %id = call i32 @llvm.eh.typeid.for(ptr @typeinfo)
  %match = icmp eq i32 %s, %id
  br i1 %match, label %matched, label %resume
matched:
  invoke void @cleanup(ptr %p, i32 %s) to label %normal unwind label %nested
nested:
  %second = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %second
resume:
  resume { ptr, i32 } %lp
}
