// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/Inputs/scalar-calls.c -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
// RUN:   -stop-after=mmix-isel %t.ll -o - | FileCheck %s --check-prefix=ISEL

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S %S/Inputs/scalar-calls.c -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --implicit-check-not=MMIXAL < %t.s

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -c %S/Inputs/scalar-calls.c -o %t.o
// RUN: llvm-readobj --file-headers --symbols --relocations --expand-relocs \
// RUN:   %t.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// IR-LABEL: define dso_local i64 @local_add(i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}})
// IR-LABEL: define dso_local i64 @recursive_sum(i64 noundef %{{[0-9]+}})
// IR: call i64 @recursive_sum(i64 noundef
// IR-LABEL: define dso_local i64 @call_local(i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}})
// IR: call i64 @local_add(i64 noundef
// IR-LABEL: define dso_local i64 @call_indirect(ptr noundef %{{[0-9]+}},
// IR: call i64 %{{[0-9]+}}(i64 noundef
// IR-LABEL: define dso_local i64 @call_external_many(
// IR: call i64 @external_many(i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}})

// ISEL-LABEL: name: call_external_many
// ISEL: ADJCALLSTACKDOWN 16, 0
// ISEL: STOUI {{.*}}, {{%[0-9]+}}, 8
// ISEL: STOUI {{.*}}, {{%[0-9]+}}, 0
// ISEL: CALL_STATE @external_many{{.*}}implicit $r231, implicit $r232, implicit $r233, implicit $r234, implicit $r235, implicit $r236, implicit $r237, implicit $r238, implicit $r239, implicit $r240, implicit $r241, implicit $r242, implicit $r243, implicit $r244, implicit $r245, implicit $r246

// ASM-LABEL: recursive_sum:
// ASM: PUSHJB r31, recursive_sum
// ASM-LABEL: call_local:
// ASM: PUSHJB r31, local_add
// ASM-LABEL: call_indirect:
// ASM: PUSHGO r31, r{{[0-9]+}}, 0
// ASM-LABEL: call_external_many:
// ASM: STOU {{r[0-9]+}}, [[OUT:r[0-9]+]], 8
// ASM: STOU {{r[0-9]+}}, [[OUT]], 0
// ASM: GETA r{{[0-9]+}}, %geta(external_many)
// ASM: PUSHGO r31, r{{[0-9]+}}, 0

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: R_MMIX_PUSHJ_STUBBABLE (36)
// ELF-NEXT: Symbol: recursive_sum
// ELF: Type: R_MMIX_PUSHJ_STUBBABLE (36)
// ELF-NEXT: Symbol: local_add
// ELF: Type: R_MMIX_PUSHJ_STUBBABLE (36)
// ELF-NEXT: Symbol: external_many
// ELF: Name: external_many
// ELF: Section: Undefined

// DIS-LABEL: <recursive_sum>:
// DIS: PUSHJB r31,
// DIS-LABEL: <call_indirect>:
// DIS: PUSHGO r31,
// DIS-LABEL: <call_external_many>:
// DIS: PUSHJ r31, 0
// DIS-NEXT: {{.*}} R_MMIX_PUSHJ_STUBBABLE external_many
