; RUN: llc -mtriple=mmix-unknown-elf -O0 -verify-machineinstrs \
; RUN:   -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-elf -O2 -verify-machineinstrs \
; RUN:   -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-elf -O2 -verify-machineinstrs \
; RUN:   -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-elf -O2 -verify-machineinstrs \
; RUN:   -filetype=obj %s -o %t.o
; RUN: llvm-readobj --relocations --expand-relocs %t.o \
; RUN:   | FileCheck %s --check-prefix=ELF
; RUN: llvm-objdump -d %t.o \
; RUN:   | FileCheck %s --check-prefix=OBJ --implicit-check-not='<unknown>'

target triple = "mmix-unknown-elf"

%small = type <{ i8, i32 }>
%large = type { i64, i64 }
%complex = type { double, double }

; Every scalar kind uses the first ordinary argument and result register.
; ISEL-LABEL: name: fast_narrow
; ISEL:       liveins: $r231
; ISEL:       RET_VALUE {{.*}}implicit $r231
define internal fastcc signext i8 @fast_narrow(
    i8 signext %value) noinline {
  ret i8 %value
}

; ISEL-LABEL: name: fast_pointer
; ISEL:       liveins: $r231
; ISEL:       RET_VALUE {{.*}}implicit $r231
define internal fastcc ptr @fast_pointer(ptr %value) noinline {
  ret ptr %value
}

; ISEL-LABEL: name: fast_float
; ISEL:       liveins: $r231
; ISEL:       RET_VALUE {{.*}}implicit $r231
define internal fastcc float @fast_float(float %value) noinline {
  ret float %value
}

; ISEL-LABEL: name: fast_double
; ISEL:       liveins: $r231
; ISEL:       RET_VALUE {{.*}}implicit $r231
define internal fastcc double @fast_double(double %value) noinline {
  ret double %value
}

; A supported direct aggregate occupies one ordinary slot in both directions.
; ISEL-LABEL: name: fast_small
; ISEL:       liveins: $r231
; ISEL:       RET_VALUE {{.*}}implicit $r231
define internal fastcc %small @fast_small(%small %value) noinline {
  ret %small %value
}

; A caller-copy aggregate arrives as an ordinary pointer in r231. The callee
; owns a 16-byte local copy, matching the C convention's byval representation.
; ISEL-LABEL: name: fast_byval
; ISEL:       liveins: $r231
; ISEL:       LDOUI {{.*}}, 0 :: (load (s64))
; ISEL:       STOUI {{.*}}, %stack.0, 0 :: (store (s64) into %stack.0)
; ISEL:       LDOUI {{.*}}, 8 :: (load (s64) from unknown-address + 8)
; ISEL:       STOUI {{.*}}, %stack.0, 8 :: (store (s64) into %stack.0 + 8)
; ISEL:       RET_VALUE {{.*}}implicit $r231
define internal fastcc i64 @fast_byval(
    ptr byval(%large) align 8 %value) noinline {
  %second = getelementptr inbounds %large, ptr %value, i64 0, i32 1
  %result = load i64, ptr %second, align 8
  ret i64 %result
}

; The hidden result pointer uses r251 without consuming r231 or r232 and is
; copied to r231 on return.
; ISEL-LABEL: name: fast_sret
; ISEL:       liveins: $r251, $r231, $r232
; ISEL:       [[SRET:%[0-9]+]]:{{[^ ]+}} = COPY $r251
; ISEL:       $r231 = COPY [[SRET]]
; ISEL:       RET_VALUE {{.*}}implicit $r231
define internal fastcc void @fast_sret(
    ptr sret(%large) align 8 %out, i64 %first, i64 %second) noinline {
  %first.ptr = getelementptr inbounds %large, ptr %out, i64 0, i32 0
  %second.ptr = getelementptr inbounds %large, ptr %out, i64 0, i32 1
  store i64 %first, ptr %first.ptr, align 8
  store i64 %second, ptr %second.ptr, align 8
  ret void
}

; Wide Complex uses a caller-copy argument and the reviewed two-register
; result representation. A separate section also exercises an ELF call
; relocation without defining a FastCC-specific relocation identity.
; ISEL-LABEL: name: fast_complex_result
; ISEL:       liveins: $r231
; ISEL:       LDOUI {{.*}}, 8 :: (load (s64) from unknown-address + 8)
; ISEL:       STOUI {{.*}}, %stack.0, 8 :: (store (s64) into %stack.0 + 8)
; ISEL:       LDOUI {{.*}}, 0 :: (load (s64))
; ISEL:       STOUI {{.*}}, %stack.0, 0 :: (store (s64) into %stack.0)
; ISEL:       RET_PAIR {{.*}}implicit $r231, implicit $r232
define internal fastcc %complex @fast_complex_result(
    ptr byval(%complex) align 8 %value) noinline section ".text.fastcc" {
  %real.ptr = getelementptr inbounds %complex, ptr %value, i64 0, i32 0
  %imag.ptr = getelementptr inbounds %complex, ptr %value, i64 0, i32 1
  %real = load double, ptr %real.ptr, align 8
  %imag = load double, ptr %imag.ptr, align 8
  %part = insertvalue %complex poison, double %real, 0
  %result = insertvalue %complex %part, double %imag, 1
  ret %complex %result
}

; This C-convention wrapper proves matching FastCC call assignments at -O0 and
; -O2. Each call carries the ordinary csr_mmix preservation contract.
; ISEL-LABEL: name: exercise_fastcc
; ISEL:       CALL_STATE @fast_narrow, csr_mmix{{.*}}implicit $r231{{.*}}implicit-def $r231
; ISEL:       CALL_STATE @fast_pointer, csr_mmix{{.*}}implicit $r231{{.*}}implicit-def $r231
; ISEL:       CALL_STATE @fast_float, csr_mmix{{.*}}implicit $r231{{.*}}implicit-def $r231
; ISEL:       CALL_STATE @fast_double, csr_mmix{{.*}}implicit $r231{{.*}}implicit-def $r231
; ISEL:       CALL_STATE @fast_small, csr_mmix{{.*}}implicit $r231{{.*}}implicit-def $r231
; ISEL:       CALL_STATE @fast_byval, csr_mmix{{.*}}implicit $r231{{.*}}implicit-def $r231
; ISEL:       CALL_STATE @fast_sret, csr_mmix{{.*}}implicit $r251, implicit $r231, implicit $r232
; ISEL:       CALL_STATE @fast_complex_result, {{.*}}csr_mmix{{.*}}implicit $r231{{.*}}implicit-def $r231, implicit-def $r232
; ASM-LABEL: exercise_fastcc:
; ASM:       PUSHJB r31, fast_narrow
; ASM:       PUSHJB r31, fast_pointer
; ASM:       PUSHJB r31, fast_float
; ASM:       PUSHJB r31, fast_double
; ASM:       PUSHJB r31, fast_small
; ASM:       PUSHJB r31, fast_byval
; ASM:       PUSHJB r31, fast_sret
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
define i64 @exercise_fastcc(ptr %pointer, i64 %bits, ptr %copy, ptr %out) {
  %narrow = trunc i64 %bits to i8
  %narrow.result = call fastcc signext i8 @fast_narrow(
      i8 signext %narrow)
  %pointer.result = call fastcc ptr @fast_pointer(ptr %pointer)
  %float.bits = trunc i64 %bits to i32
  %float.value = bitcast i32 %float.bits to float
  %float.result = call fastcc float @fast_float(float %float.value)
  %double.value = bitcast i64 %bits to double
  %double.result = call fastcc double @fast_double(double %double.value)
  %small.0 = insertvalue %small poison, i8 %narrow.result, 0
  %small.1 = insertvalue %small %small.0, i32 %float.bits, 1
  %small.result = call fastcc %small @fast_small(%small %small.1)
  %byval.result = call fastcc i64 @fast_byval(
      ptr byval(%large) align 8 %copy)
  call fastcc void @fast_sret(
      ptr sret(%large) align 8 %out, i64 %bits, i64 %byval.result)
  %complex.result = call fastcc %complex @fast_complex_result(
      ptr byval(%complex) align 8 %copy)
  %small.word = extractvalue %small %small.result, 1
  %small.wide = zext i32 %small.word to i64
  %pointer.bits = ptrtoint ptr %pointer.result to i64
  %float.out = bitcast float %float.result to i32
  %float.wide = zext i32 %float.out to i64
  %double.out = bitcast double %double.result to i64
  %complex.imag = extractvalue %complex %complex.result, 1
  %complex.bits = bitcast double %complex.imag to i64
  %sum0 = add i64 %pointer.bits, %small.wide
  %sum1 = add i64 %sum0, %float.wide
  %sum2 = add i64 %sum1, %double.out
  %sum3 = add i64 %sum2, %complex.bits
  ret i64 %sum3
}

; Sixteen scalar slots consume r231-r246. The direct aggregate and caller-copy
; pointer then occupy incoming stack offsets zero and eight.
; ISEL-LABEL: name: fast_exhausted
; ISEL:       fixedStack:
; ISEL-DAG:   offset: 0, size: 8, alignment: 8
; ISEL-DAG:   offset: 8, size: 8, alignment: 8
; ISEL:       RET_VALUE {{.*}}implicit $r231
define internal fastcc %small @fast_exhausted(
    i64, i64, i64, i64, i64, i64, i64, i64,
    i64, i64, i64, i64, i64, i64, i64, i64,
    %small %direct, ptr byval(%large) align 8 %copy) noinline {
  %word = extractvalue %small %direct, 1
  %copy.second = getelementptr inbounds %large, ptr %copy, i64 0, i32 1
  %loaded = load i64, ptr %copy.second, align 8
  %loaded.word = trunc i64 %loaded to i32
  %sum = add i32 %word, %loaded.word
  %result = insertvalue %small %direct, i32 %sum, 1
  ret %small %result
}

; ISEL-LABEL: name: exercise_fastcc_stack
; ISEL:       ADJCALLSTACKDOWN 16, 0
; ISEL-DAG:   STOUI {{.*}}, 0 :: (store (s64) into stack)
; ISEL-DAG:   STOUI {{.*}}, {{.*}}, 8 :: (store (s64) into stack + 8)
; ISEL:       CALL_STATE @fast_exhausted, csr_mmix{{.*}}implicit $r231{{.*}}implicit $r246
; ASM-LABEL: exercise_fastcc_stack:
; ASM:       PUSHJB r31, fast_exhausted
define i32 @exercise_fastcc_stack(%small %value, ptr %copy) {
  %result = call fastcc %small @fast_exhausted(
      i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7,
      i64 8, i64 9, i64 10, i64 11, i64 12, i64 13, i64 14, i64 15,
      %small %value, ptr byval(%large) align 8 %copy)
  %word = extractvalue %small %result, 1
  ret i32 %word
}

; The same stack boundary is compiled under LLVM's size-oriented function
; policy rather than through a nonexistent llc -Os mode.
; ISEL-LABEL: name: exercise_fastcc_size
; ISEL:       ADJCALLSTACKDOWN 16, 0
; ISEL:       CALL_STATE @fast_exhausted, csr_mmix
; ASM-LABEL: exercise_fastcc_size:
; ASM:       PUSHJB r31, fast_exhausted
define i32 @exercise_fastcc_size(%small %value, ptr %copy) optsize {
  %result = call fastcc %small @fast_exhausted(
      i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7,
      i64 8, i64 9, i64 10, i64 11, i64 12, i64 13, i64 14, i64 15,
      %small %value, ptr byval(%large) align 8 %copy)
  %word = extractvalue %small %result, 1
  ret i32 %word
}

; ELF:      Type: R_MMIX_GETA (13)
; ELF-NEXT: Symbol: .text.fastcc
; OBJ-LABEL: <exercise_fastcc>:
; OBJ:       PUSHJ{{B?}} r31,
; OBJ-LABEL: <exercise_fastcc_stack>:
; OBJ:       PUSHJ{{B?}} r31,
