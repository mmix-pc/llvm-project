// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/Inputs/runtime-helpers.c -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -S %S/Inputs/runtime-helpers.c -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --implicit-check-not=MMIXAL < %t.s

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x -O1 \
// RUN:   -c %S/Inputs/runtime-helpers.c -o %t.o
// RUN: llvm-readobj --file-headers --symbols --relocations --expand-relocs \
// RUN:   %t.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// IR-LABEL: define dso_local void @copy_large(
// IR: call void @llvm.memcpy.p0.p0.i64({{.*}}i64 128, i1 false)
// IR-LABEL: define dso_local i32 @find_first_set(i64 noundef %{{[0-9]+}})
// IR: call i32 @__ffsdi2(i64 noundef %{{[0-9]+}})
// IR-LABEL: define dso_local double @remainder_double(double noundef %{{[0-9]+}}, double noundef %{{[0-9]+}})
// IR: call double @fmod(double noundef %{{[0-9]+}}, double noundef %{{[0-9]+}})
// IR-LABEL: define dso_local float @fused_float(float noundef %{{[0-9]+}}, float noundef %{{[0-9]+}}, float noundef %{{[0-9]+}})
// IR: call float @fmaf(float noundef %{{[0-9]+}}, float noundef %{{[0-9]+}}, float noundef %{{[0-9]+}})

// ASM-LABEL: copy_large:
// ASM: GETA r{{[0-9]+}}, %geta(memcpy)
// ASM: PUSHGO r31, r{{[0-9]+}}, 0
// ASM-LABEL: find_first_set:
// ASM: GETA r{{[0-9]+}}, %geta(__ffsdi2)
// ASM-NOT: PUSHGO
// ASM: GO r255, r{{[0-9]+}}, 0
// ASM-LABEL: remainder_double:
// ASM: GETA r{{[0-9]+}}, %geta(fmod)
// ASM-NOT: PUSHGO
// ASM: GO r255, r{{[0-9]+}}, 0
// ASM-LABEL: fused_float:
// ASM: GETA r{{[0-9]+}}, %geta(fmaf)
// ASM-NOT: PUSHGO
// ASM: GO r255, r{{[0-9]+}}, 0

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: Relocatable
// ELF: Type: R_MMIX_PUSHJ_STUBBABLE (36)
// ELF-NEXT: Symbol: memcpy
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: __ffsdi2
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: fmod
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: fmaf
// ELF: Name: memcpy
// ELF: Section: Undefined
// ELF: Name: __ffsdi2
// ELF: Section: Undefined
// ELF: Name: fmod
// ELF: Section: Undefined
// ELF: Name: fmaf
// ELF: Section: Undefined

// DIS-LABEL: <copy_large>:
// DIS: PUSHJ r31, 0
// DIS-NEXT: {{.*}} R_MMIX_PUSHJ_STUBBABLE memcpy
// DIS-LABEL: <find_first_set>:
// DIS: GETA
// DIS-NEXT: {{.*}} R_MMIX_GETA __ffsdi2
// DIS: GO r255
// DIS-LABEL: <remainder_double>:
// DIS: GETA
// DIS-NEXT: {{.*}} R_MMIX_GETA fmod
// DIS: GO r255
// DIS-LABEL: <fused_float>:
// DIS: GETA
// DIS-NEXT: {{.*}} R_MMIX_GETA fmaf
// DIS: GO r255
