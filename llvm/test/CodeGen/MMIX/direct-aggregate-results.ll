; RUN: llc -mtriple=mmix -verify-machineinstrs -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix -verify-machineinstrs -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj %s -o /dev/null
; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs -filetype=obj %s -o /dev/null
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs -filetype=obj %s -o /dev/null

target triple = "mmix"

%empty = type {}
%packed5 = type <{ i8, i32 }>
%nested6 = type { i8, { i16, i8 } }
%bytes3 = type [3 x i8]
%union8 = type { i64 }
%bitfield4 = type { i32 }

declare %empty @make_empty()
declare %packed5 @make_packed(i8, i32)
declare %nested6 @make_nested(i16)
declare %bytes3 @make_bytes(i8, i8, i8)

; A packed five-byte object is returned without extension. The leading byte
; occupies bits 32 through 39 and the trailing word occupies the low bits.
; ISEL-LABEL: name: return_packed
; ISEL:       [[TAIL:%[0-9]+]]:{{[^ ]+}} = AND
; ISEL:       [[LEAD:%[0-9]+]]:{{[^ ]+}} = ANDI {{.*}}, 255
; ISEL:       [[SHIFTED:%[0-9]+]]:{{[^ ]+}} = SLUI killed [[LEAD]], 32
; ISEL:       [[PACKED:%[0-9]+]]:{{[^ ]+}} = OR killed [[SHIFTED]], killed [[TAIL]]
; ISEL:       $r231 = COPY [[PACKED]]
; ISEL:       RET_VALUE {{.*}}implicit $r231
define %packed5 @return_packed(i8 %head, i32 %tail) {
  %v0 = insertvalue %packed5 poison, i8 %head, 0
  %v1 = insertvalue %packed5 %v0, i32 %tail, 1
  ret %packed5 %v1
}

; Nested field offsets include the aggregate's internal and trailing padding.
; The nested i16 starts two bytes from the beginning of the six-byte object.
; ISEL-LABEL: name: return_nested
; ISEL:       [[WORD:%[0-9]+]]:{{[^ ]+}} = SLUI {{.*}}, 16
; ISEL:       $r231 = COPY {{.*}}[[WORD]]
define %nested6 @return_nested(i16 %word) {
  %value = insertvalue %nested6 zeroinitializer, i16 %word, 1, 0
  ret %nested6 %value
}

; Odd-sized arrays retain object order in the low 24 result bits.
; ISEL-LABEL: name: return_bytes
; ISEL:       SLUI {{.*}}, 8
; ISEL:       SLUI {{.*}}, 16
; ISEL:       $r231 = COPY
define %bytes3 @return_bytes(i8 %a, i8 %b, i8 %c) {
  %v0 = insertvalue %bytes3 poison, i8 %a, 0
  %v1 = insertvalue %bytes3 %v0, i8 %b, 1
  %v2 = insertvalue %bytes3 %v1, i8 %c, 2
  ret %bytes3 %v2
}

; LLVM represents a union coercion by its selected storage member. Its complete
; object bits are returned in the same direct slot.
; ASM-LABEL: return_union_storage:
; ASM-NEXT:  # %bb.0:
; ASM-NEXT:  POP 0, 0
define %union8 @return_union_storage(i64 %bits) nounwind {
  %value = insertvalue %union8 poison, i64 %bits, 0
  ret %union8 %value
}

; A frontend-selected bit-field container is an object representation, not a
; signed integer result. Only its low 32 object bits are transferred.
; ISEL-LABEL: name: return_bitfield_container
; ISEL:       [[BITS:%[0-9]+]]:{{[^ ]+}} = AND {{.*}}
; ISEL:       $r231 = COPY [[BITS]]
define %bitfield4 @return_bitfield_container(i32 %bits) {
  %value = insertvalue %bitfield4 poison, i32 %bits, 0
  ret %bitfield4 %value
}

; The caller receives one octa and reconstructs packed fields at their object
; offsets. The trailing word is already in the low bits.
; ISEL-LABEL: name: call_packed_tail
; ISEL:       DIRECT_CALL_STATE @make_packed, {{.*}}implicit-def $r231
; ISEL:       [[PACKED:%[0-9]+]]:{{[^ ]+}} = COPY $r231
; ISEL:       $r231 = COPY [[PACKED]]
define i32 @call_packed_tail(i8 %head, i32 %tail) {
  %value = call %packed5 @make_packed(i8 %head, i32 %tail)
  %field = extractvalue %packed5 %value, 1
  ret i32 %field
}

; Leading fields are shifted back down from the right-justified object bits.
; ASM-LABEL: call_packed_head:
; ASM:       PUSHGO
; ASM:       SRU r231, r231, 32
; ASM:       POP 0, 0
define i8 @call_packed_head(i8 %head, i32 %tail) {
  %value = call %packed5 @make_packed(i8 %head, i32 %tail)
  %field = extractvalue %packed5 %value, 0
  ret i8 %field
}

; An indirect function call uses the same single-octa result contract.
; ISEL-LABEL: name: call_nested_indirect
; ISEL:       CALL_STATE {{.*}}implicit-def $r231
; ISEL:       [[PACKED:%[0-9]+]]:{{[^ ]+}} = COPY $r231
; ISEL:       SRUI [[PACKED]], 16
define i16 @call_nested_indirect(ptr %callee, i16 %word) {
  %value = call %nested6 %callee(i16 %word)
  %field = extractvalue %nested6 %value, 1, 0
  ret i16 %field
}

; Odd-sized call results preserve each byte's object position.
; ASM-LABEL: call_middle_byte:
; ASM:       PUSHGO
; ASM:       SRU r231, r231, 8
; ASM:       POP 0, 0
define i8 @call_middle_byte(i8 %a, i8 %b, i8 %c) {
  %value = call %bytes3 @make_bytes(i8 %a, i8 %b, i8 %c)
  %field = extractvalue %bytes3 %value, 1
  ret i8 %field
}

; Empty aggregate results do not define or read the result register.
; ISEL-LABEL: name: return_empty
; ISEL:       RET implicit $rj
; ISEL-NOT:   implicit $r231
define %empty @return_empty() {
  ret %empty zeroinitializer
}

; ISEL-LABEL: name: call_empty
; ISEL:       DIRECT_CALL_STATE @make_empty
; ISEL-NOT:   implicit-def $r231
; ISEL:       RET implicit $rj
define void @call_empty() {
  %ignored = call %empty @make_empty()
  ret void
}
