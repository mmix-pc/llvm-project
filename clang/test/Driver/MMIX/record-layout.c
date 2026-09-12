// RUN: %clang --target=mmix-unknown-unknown -std=gnu2x \
// RUN:   -Xclang -fdump-record-layouts-simple -fsyntax-only \
// RUN:   %S/Inputs/record-layout.c 2>&1 | FileCheck %s --check-prefix=LAYOUT

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/Inputs/record-layout.c -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -S %S/Inputs/record-layout.c -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --implicit-check-not=MMIXAL < %t.s

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -c %S/Inputs/record-layout.c -o %t.o
// RUN: llvm-readobj --file-headers --sections --symbols --relocations \
// RUN:   --expand-relocs %t.o | FileCheck %s --check-prefix=ELF \
// RUN:   --implicit-check-not=R_MMIX_
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// LAYOUT-LABEL: Type: struct Inner
// LAYOUT: Size:64
// LAYOUT: Alignment:32
// LAYOUT: FieldOffsets: [0, 32]
// LAYOUT-LABEL: Type: struct Nested
// LAYOUT: Size:192
// LAYOUT: Alignment:64
// LAYOUT: FieldOffsets: [0, 32, 128]
// LAYOUT-LABEL: Type: struct ArrayRecord
// LAYOUT: Size:64
// LAYOUT: Alignment:16
// LAYOUT: FieldOffsets: [0, 16]
// LAYOUT-LABEL: Type: union Choice
// LAYOUT: Size:64
// LAYOUT: Alignment:64
// LAYOUT: FieldOffsets: [0, 0]
// LAYOUT-LABEL: Type: struct Packed
// LAYOUT: Size:40
// LAYOUT: Alignment:8
// LAYOUT: FieldOffsets: [0, 8]
// LAYOUT-LABEL: Type: struct Bits
// LAYOUT: Size:24
// LAYOUT: Alignment:8
// LAYOUT: FieldOffsets: [0, 3, 8]
// LAYOUT-LABEL: Type: struct ZeroWidth
// LAYOUT: Size:128
// LAYOUT: Alignment:64
// LAYOUT: FieldOffsets: [0, 64, 64]

// IR-LABEL: define dso_local i64 @read_nested(
// The nested int and trailing long are at byte offsets 8 and 16.
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 8
// IR: load i32, ptr %{{[0-9]+}}, align 4
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 16
// IR: load i64, ptr %{{[0-9]+}}, align 8
// IR-LABEL: define dso_local i16 @read_array_member(
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 2
// IR: getelementptr inbounds{{.*}} [2 x i8], ptr %{{[0-9]+}}, i64 %{{[0-9]+}}
// IR: load i16, ptr %{{[0-9]+}}, align 2
// IR-LABEL: define dso_local i8 @read_union_byte(
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 %{{[0-9]+}}
// IR: load i8, ptr %{{[0-9]+}}, align 1
// IR-LABEL: define dso_local void @write_union_whole(
// IR: store i64 %{{[0-9]+}}, ptr %{{[0-9]+}}, align 8
// IR-LABEL: define dso_local i32 @read_packed_value(
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 1
// IR: load i32, ptr %{{[0-9]+}}, align 1
// The first field occupies the high three bits of the first byte.
// IR-LABEL: define dso_local{{.*}} i32 @read_first_bits(
// IR: load i8, ptr %{{[0-9]+}}, align 1
// IR: lshr i8 %{{[0-9]+}}, 5
// The second field occupies the remaining low five bits.
// IR-LABEL: define dso_local{{.*}} i32 @read_second_bits(
// IR: load i8, ptr %{{[0-9]+}}, align 1
// IR: and i8 %{{[0-9]+}}, 31
// The nine-bit field begins at the high bit of byte offset one.
// IR-LABEL: define dso_local{{.*}} i32 @read_wide_bits(
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 1
// IR: load i16, ptr %{{[0-9]+}}, align 1
// IR: lshr i16 %{{[0-9]+}}, 7
// A zero-width field starts the next 64-bit allocation unit.
// IR-LABEL: define dso_local{{.*}} i32 @read_after_zero_width(
// IR: getelementptr inbounds{{.*}} i8, ptr %{{[0-9]+}}, i64 8
// IR: load i8, ptr %{{[0-9]+}}, align 8
// IR: lshr i8 %{{[0-9]+}}, 5

// ASM-LABEL: read_nested:
// ASM: LDT {{r[0-9]+}}, r231, 8
// ASM: LDOU {{r[0-9]+}}, r231, 16
// ASM-LABEL: read_array_member:
// ASM: 2ADDU [[ELEMENT:r[0-9]+]], r232, r231
// ASM: LDWU r231, [[ELEMENT]], 2
// ASM-LABEL: read_union_byte:
// ASM: LDBU r231, r231, r232
// ASM-LABEL: write_union_whole:
// ASM: STOU r232, r231, 0
// ASM-LABEL: read_packed_value:
// ASM-NOT: LDTU
// ASM-COUNT-4: LDBU
// ASM: SLU {{r[0-9]+}}, {{r[0-9]+}}, 24
// ASM-LABEL: read_first_bits:
// ASM: LDBU
// ASM: SRU r231, {{r[0-9]+}}, 5
// ASM-LABEL: read_second_bits:
// ASM: LDBU
// ASM: AND r231, {{r[0-9]+}}, 31
// ASM-LABEL: read_wide_bits:
// ASM: LDBU {{r[0-9]+}}, r231, 2
// ASM: LDBU {{r[0-9]+}}, r231, 1
// ASM: SLU {{r[0-9]+}}, {{r[0-9]+}}, 8
// ASM: SRU r231, {{r[0-9]+}}, 7
// ASM-LABEL: read_after_zero_width:
// ASM: LDBU {{r[0-9]+}}, r231, 8
// ASM: SRU r231, {{r[0-9]+}}, 5

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: Relocatable
// ELF: Machine: EM_MMIX
// ELF: Name: .text
// ELF: Type: SHT_PROGBITS
// ELF: Name: read_nested
// ELF: Type: Function
// ELF: Name: read_array_member
// ELF: Type: Function
// ELF: Name: read_union_byte
// ELF: Type: Function
// ELF: Name: write_union_whole
// ELF: Type: Function
// ELF: Name: read_packed_value
// ELF: Type: Function
// ELF: Name: read_first_bits
// ELF: Type: Function
// ELF: Name: read_wide_bits
// ELF: Type: Function
// ELF: Name: read_after_zero_width
// ELF: Type: Function

// DIS-LABEL: <read_nested>:
// DIS: LDT {{r[0-9]+}}, r231, 8
// DIS: LDOU {{r[0-9]+}}, r231, 16
// DIS-LABEL: <read_array_member>:
// DIS: 2ADDU
// DIS: LDWU
// DIS-LABEL: <read_union_byte>:
// DIS: LDBU
// DIS-LABEL: <write_union_whole>:
// DIS: STOU
// DIS-LABEL: <read_packed_value>:
// DIS-NOT: LDTU
// DIS: LDBU {{r[0-9]+}}, r231, 3
// DIS: LDBU {{r[0-9]+}}, r231, 4
// DIS: LDBU {{r[0-9]+}}, r231, 2
// DIS: LDBU {{r[0-9]+}}, r231, 1
// DIS-LABEL: <read_first_bits>:
// DIS: LDBU
// DIS: SRU
// DIS-LABEL: <read_second_bits>:
// DIS: LDBU
// DIS: AND
// DIS-LABEL: <read_wide_bits>:
// DIS: LDBU {{r[0-9]+}}, r231, 2
// DIS: LDBU {{r[0-9]+}}, r231, 1
// DIS: SRU
// DIS-LABEL: <read_after_zero_width>:
// DIS: LDBU {{r[0-9]+}}, r231, 8
// DIS: SRU
