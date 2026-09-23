// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -E \
// RUN:   %S/../../CodeGen/mmix-abi-backend-integration.c -o %t.i
// RUN: FileCheck %s --check-prefix=PP < %t.i

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S -emit-llvm %S/../../CodeGen/mmix-abi-backend-integration.c \
// RUN:   -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -c -emit-llvm %S/../../CodeGen/mmix-abi-backend-integration.c \
// RUN:   -o %t.bc
// RUN: llvm-dis %t.bc -o - | FileCheck %s --check-prefix=IR

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -S \
// RUN:   %S/../../CodeGen/mmix-abi-backend-integration.c -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --implicit-check-not=MMIXAL \
// RUN:   --implicit-check-not=GREG --implicit-check-not=Main \
// RUN:   --implicit-check-not=OCTA --implicit-check-not=BSPEC < %t.s

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -c \
// RUN:   %S/../../CodeGen/mmix-abi-backend-integration.c -o %t.o
// RUN: llvm-readobj --file-headers --sections --symbols --relocations \
// RUN:   --expand-relocs %t.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// PP: struct Direct {
// PP: struct Large {
// PP: long global_seed = 7;
// PP: struct Large compose(
// PP: __builtin_va_start(ap, named);
// PP: long indirect = callback(unnamed);
// PP: long scalar = external_scalar(
// PP: struct Direct returned = external_direct(
// PP: long variadic = external_variadic(
// PP: void copy_block(

// IR: target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
// IR-NEXT: target triple = "mmix-unknown-unknown"
// IR: @global_seed ={{.*}} global i64 7, align 8
// IR: @global_pointer ={{.*}} global ptr @global_seed, align 8
// IR-LABEL: define dso_local void @compose(
// IR-SAME: ptr dead_on_unwind noalias writable sret(%struct.Large) align 8
// IR-SAME: i8 noundef signext
// IR-SAME: i16 noundef zeroext
// IR-SAME: i32 noext
// IR-SAME: ptr noundef byval(%struct.Large) align 8
// IR-SAME: ptr noundef
// IR-SAME: ptr noundef
// IR-SAME: i64 noundef
// IR-SAME: ...)
// IR: call void @llvm.va_start.p0(
// IR: call i64 @recursive_sum(i64 noundef
// IR: call i64 %{{[0-9]+}}(i64 noundef
// IR: call i64 @external_scalar(i8 noundef signext
// IR: call i32 @external_direct(i32 noext
// IR: call i64 (i64, ...) @external_variadic(i64 noundef
// IR: load ptr, ptr @global_pointer, align 8
// IR: call void @llvm.memcpy.p0.p0.i64(
// IR-LABEL: define internal i64 @recursive_sum(
// IR: call i64 @recursive_sum(i64 noundef

// ASM-LABEL: compose:
// ASM: PUSHJ r31, recursive_sum
// ASM: PUSHGO r31, r{{[0-9]+}}, 0
// ASM: GETA r{{[0-9]+}}, %geta(external_scalar)
// ASM: POP 0, 0
// ASM-LABEL: recursive_sum:
// ASM: PUSHJB r31, recursive_sum
// ASM: POP 0, 0
// ASM-LABEL: copy_block:
// ASM: GETA r{{[0-9]+}}, %geta(memcpy)
// ASM: PUSHGO r31, r{{[0-9]+}}, 0
// ASM: POP 0, 0
// ASM-LABEL: global_seed:
// ASM-NEXT: .8byte 7
// ASM-LABEL: global_pointer:
// ASM-NEXT: .8byte global_seed

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: Relocatable
// ELF: Machine: EM_MMIX
// ELF: Name: .rela.text
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: external_scalar
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: external_direct
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: external_variadic
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: global_pointer
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: global_seed
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: memcpy
// ELF: Name: recursive_sum
// ELF: Binding: Local
// ELF-NEXT: Type: Function
// ELF: Name: compose
// ELF: Binding: Global
// ELF-NEXT: Type: Function
// ELF: Name: global_seed
// ELF: Type: Object

// DIS-LABEL: <compose>:
// DIS: PUSHJ r31,
// DIS: PUSHGO r31, r{{[0-9]+}}, 0
// DIS: GETA {{r[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA external_scalar
// DIS: PUSHGO r31, {{r[0-9]+}}, 0
// DIS: GETA {{r[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA external_direct
// DIS: PUSHGO r31, {{r[0-9]+}}, 0
// DIS: GETA {{r[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA external_variadic
// DIS: PUSHGO r31, {{r[0-9]+}}, 0
// DIS: GETA r{{[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA global_pointer
// DIS-LABEL: <recursive_sum>:
// DIS: PUSHJB r31,
// DIS-LABEL: <copy_block>:
// DIS: GETA {{r[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA memcpy
// DIS: PUSHGO r31, {{r[0-9]+}}, 0
