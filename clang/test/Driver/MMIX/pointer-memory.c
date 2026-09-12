// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/Inputs/pointer-memory.c -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: llc -mtriple=mmix -O1 -stop-after=prolog-epilog %t.ll -o - \
// RUN:   | FileCheck %s --check-prefix=FRAME

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -S %S/Inputs/pointer-memory.c -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --implicit-check-not=MMIXAL < %t.s

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -c %S/Inputs/pointer-memory.c -o %t.o
// RUN: llvm-readobj --file-headers --sections --symbols --relocations \
// RUN:   --expand-relocs %t.o | FileCheck %s --check-prefix=ELF \
// RUN:   --implicit-check-not=R_MMIX_
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// IR: target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
// IR-LABEL: define dso_local i64 @load_constant_index(
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 24
// IR: load i64, ptr %{{[0-9]+}}, align 8
// IR-LABEL: define dso_local i64 @load_large_index(
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 256
// IR: load i64, ptr %{{[0-9]+}}, align 8
// IR-LABEL: define dso_local i32 @load_variable_index(
// IR: getelementptr inbounds{{.*}} [4 x i8], ptr %{{[0-9]+}}, i64 %{{[0-9]+}}
// IR: load i32, ptr %{{[0-9]+}}, align 4
// IR-LABEL: define dso_local void @store_variable_index(
// IR-SAME: i16 noundef zeroext
// IR: getelementptr inbounds{{.*}} [2 x i8], ptr %{{[0-9]+}}, i64 %{{[0-9]+}}
// IR: store i16 %{{[0-9]+}}, ptr %{{[0-9]+}}, align 2
// IR-LABEL: define dso_local i8 @load_byte_offset(
// IR: getelementptr inbounds i8, ptr %{{[0-9]+}}, i64 %{{[0-9]+}}
// IR: load i8, ptr %{{[0-9]+}}, align 1
// IR-LABEL: define dso_local{{.*}} i32 @addresses_ordered(
// IR: icmp ult ptr
// IR-LABEL: define dso_local void @copy_two_words(
// IR: load i64
// IR: store i64
// IR: load i64
// IR: store i64
// IR-LABEL: define dso_local{{.*}} i64 @local_objects(
// Natural C alignment remains visible in IR.
// IR: alloca [3 x i8], align 1
// IR: alloca i16, align 2
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 %{{[0-9]+}}
// IR: load volatile i8, ptr %{{[0-9]+}}, align 1
// IR: load volatile i16, ptr %{{[0-9]+}}, align 2

// MMIX raises only fixed automatic-object placement to four-byte alignment.
// FRAME-LABEL: name: local_objects
// FRAME: stack:
// FRAME: type: default,{{.*}}size: 3, alignment: 4
// FRAME: type: default,{{.*}}size: 2, alignment: 4

// ASM-LABEL: load_constant_index:
// ASM: LDOU r231, r231, 24
// ASM-LABEL: load_large_index:
// ASM: SETL [[LARGE:r[0-9]+]], 256
// ASM: LDOU r231, r231, [[LARGE]]
// ASM-LABEL: load_variable_index:
// ASM: SLU [[WORD_OFFSET:r[0-9]+]], r232, 2
// ASM: LDTU r231, r231, [[WORD_OFFSET]]
// ASM-LABEL: store_variable_index:
// ASM: SLU [[HALF_OFFSET:r[0-9]+]], r232, 1
// ASM: STWU r233, r231, [[HALF_OFFSET]]
// ASM-LABEL: load_byte_offset:
// ASM: LDBU r231, r231, r232
// ASM-LABEL: addresses_ordered:
// ASM: CMPU
// ASM: ZSN
// ASM-LABEL: copy_two_words:
// ASM: LDOU {{r[0-9]+}}, r232, 0
// ASM: STOU {{r[0-9]+}}, r231, 0
// ASM: LDOU {{r[0-9]+}}, r232, 8
// ASM: STOU {{r[0-9]+}}, r231, 8
// ASM-LABEL: local_objects:
// ASM: SUBU r254, r254, 16
// ASM: STBU
// ASM: STBU
// ASM: STBU
// ASM: STWU
// ASM: LDBU
// ASM: LDWU

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: Relocatable
// ELF: Machine: EM_MMIX
// ELF: Name: .text
// ELF: Type: SHT_PROGBITS
// ELF: Name: load_constant_index
// ELF: Type: Function
// ELF: Name: load_variable_index
// ELF: Type: Function
// ELF: Name: store_variable_index
// ELF: Type: Function
// ELF: Name: addresses_ordered
// ELF: Type: Function
// ELF: Name: copy_two_words
// ELF: Type: Function
// ELF: Name: local_objects
// ELF: Type: Function

// DIS-LABEL: <load_constant_index>:
// DIS: LDOU r231, r231, 24
// DIS-LABEL: <load_large_index>:
// DIS: SETL {{r[0-9]+}}, 256
// DIS: LDOU
// DIS-LABEL: <load_variable_index>:
// DIS: SLU
// DIS: LDTU
// DIS-LABEL: <store_variable_index>:
// DIS: SLU
// DIS: STWU
// DIS-LABEL: <load_byte_offset>:
// DIS: LDBU r231, r231, r232
// DIS-LABEL: <addresses_ordered>:
// DIS: CMPU
// DIS-LABEL: <copy_two_words>:
// DIS: LDOU
// DIS: STOU
// DIS: LDOU
// DIS: STOU
// DIS-LABEL: <local_objects>:
// DIS: STBU
// DIS: STWU
// DIS: LDBU
// DIS: LDWU
