; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
; RUN:   -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=asm \
; RUN:   %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
; RUN:   %s -o %t.o
; RUN: llvm-readobj --relocations --expand-relocs --symbols %t.o \
; RUN:   | FileCheck %s --check-prefix=ELF
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
; RUN:   | FileCheck %s --check-prefix=OBJ --implicit-check-not='<unknown>'

target triple = "mmix-unknown-elf"

%small = type <{ i8, i32 }>
%large = type { i64, i64 }

@aggregate_source = global %large { i64 17, i64 23 }, align 8
@aggregate_result = global %large zeroinitializer, align 8

declare %small @external_direct(%small, ptr byval(%large) align 8)
declare void @external_sret(ptr sret(%large) align 8, i64)
declare void @external_exhausted(
    ptr sret(%large) align 8,
    i64, i64, i64, i64, i64, i64, i64, i64,
    i64, i64, i64, i64, i64, i64, i64, i64,
    %small, ptr byval(%large) align 8)

; This recursive callee composes the dedicated result pointer, a direct
; aggregate, and a caller-copy argument. Its own $251 value survives the
; nested call and is returned in $231.
; ISEL-LABEL: name: recursive_result
; ISEL:       liveins: $r251, $r231, $r232, $r233
; ISEL:       $r251 = COPY [[SRET:%[0-9]+]]
; ISEL:       $r231 = COPY
; ISEL:       $r232 = COPY
; ISEL:       $r233 = COPY
; ISEL:       CALL_STATE @recursive_result{{.*}}implicit $r251, implicit $r231, implicit $r232, implicit $r233
; ISEL:       $r231 = COPY [[SRET]]
; ISEL-NEXT:  RET_VALUE {{.*}}implicit $r231
; OBJ-LABEL: <recursive_result>:
; OBJ:       PUSHJB r31,
define internal void @recursive_result(
    ptr sret(%large) align 8 %out, i64 %count, %small %seed,
    ptr byval(%large) align 8 %copy) {
entry:
  %done = icmp eq i64 %count, 0
  br i1 %done, label %base, label %recurse

base:
  %seed_word = extractvalue %small %seed, 1
  %extended = zext i32 %seed_word to i64
  %first = getelementptr inbounds %large, ptr %out, i64 0, i32 0
  store i64 %extended, ptr %first, align 8
  br label %return

recurse:
  %next = sub i64 %count, 1
  call void @recursive_result(
      ptr sret(%large) align 8 %out, i64 %next, %small %seed,
      ptr byval(%large) align 8 %copy)
  %source_second = getelementptr inbounds %large, ptr %copy, i64 0, i32 1
  %source_value = load i64, ptr %source_second, align 8
  %result_second = getelementptr inbounds %large, ptr %out, i64 0, i32 1
  store i64 %source_value, ptr %result_second, align 8
  br label %return

return:
  ret void
}

; A fixed-frame-pointer function consumes a direct aggregate result, creates
; caller-owned copies for two calls, and retains its incoming sret pointer.
; ISEL-LABEL: name: frame_result
; ISEL:       DIRECT_CALL_STATE @external_direct{{.*}}implicit $r231, implicit $r232{{.*}}implicit-def $r231
; ISEL:       $r251 = COPY [[SRET:%[0-9]+]]
; ISEL:       CALL_STATE @recursive_result{{.*}}implicit $r251, implicit $r231, implicit $r232, implicit $r233
; ISEL:       $r231 = COPY [[SRET]]
; ASM-LABEL: frame_result:
; ASM:       STOU r253, r254,
; ASM:       ADDU r253, r254,
; ASM:       GETA [[EXTERNAL:r[0-9]+]], %geta(external_direct)
; ASM:       PUSHGO r31, [[EXTERNAL]], 0
; ASM:       PUSHJB r31, recursive_result
define void @frame_result(
    ptr sret(%large) align 8 %out, %small %seed,
    ptr byval(%large) align 8 %copy) #0 {
entry:
  %direct = call %small @external_direct(
      %small %seed, ptr byval(%large) align 8 %copy)
  call void @recursive_result(
      ptr sret(%large) align 8 %out, i64 1, %small %direct,
      ptr byval(%large) align 8 %copy)
  ret void
}

; Indirect callees receive the same composed register assignment and use the
; register-indirect call path.
; ISEL-LABEL: name: indirect_result_chain
; ISEL:       $r251 = COPY [[SRET:%[0-9]+]]
; ISEL:       $r231 = COPY
; ISEL:       $r232 = COPY
; ISEL:       CALL_STATE {{%[0-9]+}}{{.*}}implicit $r251, implicit $r231, implicit $r232
; ISEL:       $r231 = COPY [[SRET]]
; ASM-LABEL: indirect_result_chain:
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
define void @indirect_result_chain(
    ptr sret(%large) align 8 %out, ptr %callee, %small %seed,
    ptr byval(%large) align 8 %copy) {
entry:
  call void %callee(
      ptr sret(%large) align 8 %out, %small %seed,
      ptr byval(%large) align 8 %copy)
  ret void
}

; $251 does not consume an ordinary slot. Sixteen scalars fill $231-$246,
; after which the direct aggregate and byval pointer use stack offsets 0 and 8.
; ISEL-LABEL: name: call_exhausted
; ISEL:       ADJCALLSTACKDOWN 16, 0
; ISEL:       STOUI {{.*}}, {{%[0-9]+}}, 8 :: (store (s64) into stack + 8)
; ISEL:       STOUI {{.*}}, {{%[0-9]+}}, 0 :: (store (s64) into stack)
; ISEL:       $r251 = COPY
; ISEL:       $r231 = COPY
; ISEL:       $r246 = COPY
; ISEL:       DIRECT_CALL_STATE @external_exhausted{{.*}}implicit $r251, implicit $r231{{.*}}implicit $r246
define void @call_exhausted(%small %seed, ptr %source, ptr %out) {
entry:
  call void @external_exhausted(
      ptr sret(%large) align 8 %out,
      i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7,
      i64 8, i64 9, i64 10, i64 11, i64 12, i64 13, i64 14, i64 15,
      %small %seed, ptr byval(%large) align 8 %source)
  ret void
}

; Global storage and text calls use canonical GETA addresses while direct ELF
; objects retain their stubbable call relocations.
; ASM-LABEL: call_globals:
; ASM:       GETA {{r[0-9]+}}, %geta(aggregate_result)
; ASM:       GETA {{r[0-9]+}}, %geta(external_sret)
; ASM:       PUSHGO
; ASM:       GETA {{r[0-9]+}}, %geta(aggregate_source)
; ASM:       GETA {{r[0-9]+}}, %geta(external_direct)
; ASM:       PUSHGO
define void @call_globals() {
entry:
  call void @external_sret(
      ptr sret(%large) align 8 @aggregate_result, i64 29)
  %ignored = call %small @external_direct(
      %small zeroinitializer,
      ptr byval(%large) align 8 @aggregate_source)
  ret void
}

; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_direct
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_exhausted
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: aggregate_result
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: external_sret
; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: aggregate_source
; ELF:      Name: aggregate_result
; ELF:      Type: Object
; ELF:      Name: aggregate_source
; ELF:      Type: Object

attributes #0 = { "frame-pointer"="all" }
