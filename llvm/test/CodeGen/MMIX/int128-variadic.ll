; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs -filetype=obj %s -o %t.linux.o
; RUN: llc -mtriple=mmix-unknown-unknown -O0 -verify-machineinstrs -filetype=obj %s -o %t.generic.o

; These consumers model the target's pointer-based va_arg expansion. They
; intentionally do not use unsupported raw LLVM va_arg or claim Clang source
; admission. One cursor step is 16 bytes with no 16-byte realignment.
declare void @llvm.va_start(ptr)
declare void @llvm.va_copy(ptr, ptr)
declare void @llvm.va_end(ptr)

; ISEL-LABEL: name: read_after_0
; ISEL: fixedStack:
; ISEL: offset: -128, size: 128, alignment: 8
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @read_after_0(...) {
 %ap = alloca ptr, align 8
 %copy = alloca ptr, align 8
 call void @llvm.va_start(ptr %ap)
 call void @llvm.va_copy(ptr %copy, ptr %ap)
 %cur = load ptr, ptr %ap, align 8
 %first = load i128, ptr %cur, align 8
 %next = getelementptr i8, ptr %cur, i64 16
 store ptr %next, ptr %ap, align 8
 %second = load i128, ptr %next, align 8
 %after = getelementptr i8, ptr %next, i64 16
 store ptr %after, ptr %ap, align 8
 %copied = load ptr, ptr %copy, align 8
 %original = load i128, ptr %copied, align 8
 %a = add i128 %first, %second
 %r = xor i128 %a, %original
 call void @llvm.va_end(ptr %copy)
 call void @llvm.va_end(ptr %ap)
 ret i128 %r
}

; ISEL-LABEL: name: read_after_14
; ISEL: fixedStack:
; ISEL: offset: -16, size: 16, alignment: 8
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @read_after_14(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, ...) {
 %ap = alloca ptr, align 8
 %copy = alloca ptr, align 8
 call void @llvm.va_start(ptr %ap)
 call void @llvm.va_copy(ptr %copy, ptr %ap)
 %cur = load ptr, ptr %ap, align 8
 %first = load i128, ptr %cur, align 8
 %next = getelementptr i8, ptr %cur, i64 16
 store ptr %next, ptr %ap, align 8
 %second = load i128, ptr %next, align 8
 %after = getelementptr i8, ptr %next, i64 16
 store ptr %after, ptr %ap, align 8
 %copied = load ptr, ptr %copy, align 8
 %original = load i128, ptr %copied, align 8
 %a = add i128 %first, %second
 %r = xor i128 %a, %original
 call void @llvm.va_end(ptr %copy)
 call void @llvm.va_end(ptr %ap)
 ret i128 %r
}

; ISEL-LABEL: name: read_after_15
; ISEL: fixedStack:
; ISEL: offset: -8, size: 8, alignment: 8
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @read_after_15(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, ...) {
 %ap = alloca ptr, align 8
 %copy = alloca ptr, align 8
 call void @llvm.va_start(ptr %ap)
 call void @llvm.va_copy(ptr %copy, ptr %ap)
 %cur = load ptr, ptr %ap, align 8
 %first = load i128, ptr %cur, align 8
 %next = getelementptr i8, ptr %cur, i64 16
 store ptr %next, ptr %ap, align 8
 %second = load i128, ptr %next, align 8
 %after = getelementptr i8, ptr %next, i64 16
 store ptr %after, ptr %ap, align 8
 %copied = load ptr, ptr %copy, align 8
 %original = load i128, ptr %copied, align 8
 %a = add i128 %first, %second
 %r = xor i128 %a, %original
 call void @llvm.va_end(ptr %copy)
 call void @llvm.va_end(ptr %ap)
 ret i128 %r
}

; ISEL-LABEL: name: read_after_16
; ISEL: fixedStack:
; ISEL: offset: 0, size: 8, alignment: 8
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @read_after_16(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, ...) {
 %ap = alloca ptr, align 8
 %copy = alloca ptr, align 8
 call void @llvm.va_start(ptr %ap)
 call void @llvm.va_copy(ptr %copy, ptr %ap)
 %cur = load ptr, ptr %ap, align 8
 %first = load i128, ptr %cur, align 8
 %next = getelementptr i8, ptr %cur, i64 16
 store ptr %next, ptr %ap, align 8
 %second = load i128, ptr %next, align 8
 %after = getelementptr i8, ptr %next, i64 16
 store ptr %after, ptr %ap, align 8
 %copied = load ptr, ptr %copy, align 8
 %original = load i128, ptr %copied, align 8
 %a = add i128 %first, %second
 %r = xor i128 %a, %original
 call void @llvm.va_end(ptr %copy)
 call void @llvm.va_end(ptr %ap)
 ret i128 %r
}

; ISEL-LABEL: name: read_after_17
; ISEL: fixedStack:
; ISEL: offset: 8, size: 8, alignment: 8
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @read_after_17(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, ...) {
 %ap = alloca ptr, align 8
 %copy = alloca ptr, align 8
 call void @llvm.va_start(ptr %ap)
 call void @llvm.va_copy(ptr %copy, ptr %ap)
 %cur = load ptr, ptr %ap, align 8
 %first = load i128, ptr %cur, align 8
 %next = getelementptr i8, ptr %cur, i64 16
 store ptr %next, ptr %ap, align 8
 %second = load i128, ptr %next, align 8
 %after = getelementptr i8, ptr %next, i64 16
 store ptr %after, ptr %ap, align 8
 %copied = load ptr, ptr %copy, align 8
 %original = load i128, ptr %copied, align 8
 %a = add i128 %first, %second
 %r = xor i128 %a, %original
 call void @llvm.va_end(ptr %copy)
 call void @llvm.va_end(ptr %ap)
 ret i128 %r
}

; A named i128 consumes two slots; the unnamed pair starts at r233.
; ISEL-LABEL: name: named_wide
; ISEL: fixedStack:
; ISEL: offset: -112, size: 112, alignment: 8
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @named_wide(i128 %named, ...) {
 %ap = alloca ptr, align 8
 call void @llvm.va_start(ptr %ap)
 %cur = load ptr, ptr %ap, align 8
 %v = load i128, ptr %cur, align 8
 %r = add i128 %v, %named
 call void @llvm.va_end(ptr %ap)
 ret i128 %r
}

; Fifteen fixed slots leave r246 for the first high word; the low word
; and the next pair occupy stack offsets 0, 8, 16. A following scalar is at 24.
; ISEL-LABEL: name: call_boundary
; ISEL: ADJCALLSTACKDOWN 32, 0
; ISEL-DAG: [[LO:%[0-9]+]]:{{[^ ]+}} = LOAD_IMM64 2
; ISEL-DAG: [[HI2:%[0-9]+]]:{{[^ ]+}} = LOAD_IMM64 3
; ISEL-DAG: [[LO2:%[0-9]+]]:{{[^ ]+}} = LOAD_IMM64 4
; ISEL-DAG: [[SCALAR:%[0-9]+]]:{{[^ ]+}} = LOAD_IMM64 5
; ISEL-DAG: STOUI {{(killed )?}}[[LO]], {{.*}}, 0 :: (store (s64) into stack)
; ISEL-DAG: STOUI {{(killed )?}}[[HI2]], {{.*}}, 8 :: (store (s64) into stack + 8)
; ISEL-DAG: STOUI {{(killed )?}}[[LO2]], {{.*}}, 16 :: (store (s64) into stack + 16)
; ISEL-DAG: STOUI {{(killed )?}}[[SCALAR]], {{.*}}, 24 :: (store (s64) into stack + 24)
; ISEL: [[HI:%[0-9]+]]:{{[^ ]+}} = LOAD_IMM64 1
; ISEL: $r246 = COPY [[HI]]
; ISEL: CALL_STATE @read_after_15
define i128 @call_boundary() {
 %r = call i128 (i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, ...) @read_after_15(
 i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0,
 i128 18446744073709551618, i128 55340232221128654852, i64 5)
 ret i128 %r
}
