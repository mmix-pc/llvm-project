// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/Inputs/integer-control.c -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -S %S/Inputs/integer-control.c -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --implicit-check-not=MMIXAL \
// RUN:   --implicit-check-not=GREG --implicit-check-not=OCTA < %t.s

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -c %S/Inputs/integer-control.c -o %t.o
// RUN: llvm-readobj --file-headers --sections --symbols --relocations \
// RUN:   --expand-relocs %t.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// IR-LABEL: define dso_local i64 @signed_arithmetic(
// IR: add nsw i64
// IR: mul nsw i64
// IR: sdiv i64
// IR: sub nsw i64
// IR-LABEL: define dso_local{{.*}} i64 @unsigned_arithmetic(
// IR: sub i64
// IR: mul i64
// IR: udiv i64
// IR: add i64
// IR-LABEL: define dso_local{{.*}} i64 @shift_logic(
// IR: and i32 %{{[0-9]+}}, 63
// IR: zext{{.*}} i32
// IR: shl i64
// IR: lshr i64
// IR: xor i64
// IR-LABEL: define dso_local{{.*}} i64 @promote_narrow(
// IR-SAME: i8 noundef signext
// IR-SAME: i16 noundef zeroext
// IR: sext i8
// IR: zext i16
// IR: add nsw i64
// IR-LABEL: define dso_local{{.*}} i64 @signed_branch(
// IR: icmp slt i64
// IR: select i1
// IR-LABEL: define dso_local{{.*}} i64 @unsigned_branch(
// IR: icmp ult i64
// IR: select i1
// IR-LABEL: define dso_local{{.*}} i64 @loop_accumulate(
// IR: phi i64
// IR: phi i64
// IR: load i64
// IR: add nsw i64
// IR: icmp eq i64
// IR: br i1
// IR-LABEL: define dso_local{{.*}} i64 @switch_value(
// IR: switch i32 %{{[0-9]+}}
// IR: i32 1, label
// IR: i32 4, label
// IR: i32 9, label

// ASM-LABEL: signed_arithmetic:
// ASM: DIV
// ASM: MULU
// ASM: ADDU
// ASM-LABEL: unsigned_arithmetic:
// ASM: DIVU
// ASM: SUBU
// ASM: MULU
// ASM: ADDU
// ASM-LABEL: shift_logic:
// ASM: SRU
// ASM: SLU
// ASM: XOR
// ASM-LABEL: promote_narrow:
// ASM: ADDU
// ASM-LABEL: signed_branch:
// ASM: CMP
// ASM-LABEL: unsigned_branch:
// ASM: ODIF
// ASM-LABEL: loop_accumulate:
// ASM: LDOU
// ASM: ADDU
// ASM: BNZB
// ASM-LABEL: switch_value:
// ASM: CMPU
// ASM: BZ
// ASM: CMPU
// ASM: BZ

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: Relocatable
// ELF: Machine: EM_MMIX
// ELF: Name: .text
// ELF: Type: SHT_PROGBITS
// ELF: Name: signed_arithmetic
// ELF: Type: Function
// ELF: Name: unsigned_arithmetic
// ELF: Type: Function
// ELF: Name: shift_logic
// ELF: Type: Function
// ELF: Name: promote_narrow
// ELF: Type: Function
// ELF: Name: signed_branch
// ELF: Type: Function
// ELF: Name: unsigned_branch
// ELF: Type: Function
// ELF: Name: loop_accumulate
// ELF: Type: Function
// ELF: Name: switch_value
// ELF: Type: Function

// DIS-LABEL: <signed_arithmetic>:
// DIS: DIV
// DIS: MULU
// DIS: ADDU
// DIS-LABEL: <unsigned_arithmetic>:
// DIS: DIVU
// DIS: SUBU
// DIS: MULU
// DIS-LABEL: <shift_logic>:
// DIS: SRU
// DIS: SLU
// DIS-LABEL: <promote_narrow>:
// DIS: ADDU
// DIS-LABEL: <signed_branch>:
// DIS: CMP
// DIS-LABEL: <unsigned_branch>:
// DIS: ODIF
// DIS-LABEL: <loop_accumulate>:
// DIS: LDOU
// DIS: BNZB
// DIS-LABEL: <switch_value>:
// DIS: CMPU
