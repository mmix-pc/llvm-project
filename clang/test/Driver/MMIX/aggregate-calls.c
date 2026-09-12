// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/Inputs/aggregate-calls.c -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
// RUN:   -stop-after=mmix-isel %t.ll -o - | FileCheck %s --check-prefix=ISEL

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S %S/Inputs/aggregate-calls.c -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --implicit-check-not=MMIXAL < %t.s

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -c %S/Inputs/aggregate-calls.c -o %t.o
// RUN: llvm-readobj --file-headers --symbols --relocations --expand-relocs \
// RUN:   %t.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// The empty argument consumes no IR or MMIX argument slot. A one-octa value
// and result stay direct.
// IR-LABEL: define dso_local i64 @forward_direct(i64 %{{[0-9]+}})
// IR: call i64 @external_direct(i64 %{{[0-9]+}})
// IR: ret i64
// IR: declare dso_local i64 @external_direct(i64)

// The hidden result pointer is separate from the 16 ordinary argument slots.
// The one-octa value occupies slot 15; the odd direct value, byval pointer,
// and tail occupy stack slots at offsets 0, 8, and 16.
// IR-LABEL: define dso_local void @forward_boundary(
// IR-SAME: ptr dead_on_unwind noalias writable sret(%struct.Large) align 8
// IR-SAME: i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 noundef %{{[0-9]+}}, i64 %{{[0-9]+}}, i64 %{{[0-9]+}}, ptr noundef byval(%struct.Large) align 8 %{{[0-9]+}}, i64 noundef %{{[0-9]+}})
// IR: lshr i64 %{{[0-9]+}}, 32
// IR: getelementptr inbounds i8, ptr %{{[0-9]+}}, i64 4
// IR: shl i64 %{{[0-9]+}}, 32
// IR: call void @external_boundary(ptr dead_on_unwind writable sret(%struct.Large) align 8
// IR-SAME: ptr noundef byval(%struct.Large) align 8

// ISEL-LABEL: name: forward_boundary
// ISEL: offset: 16, size: 8, alignment: 8
// ISEL: offset: 8, size: 8, alignment: 8
// ISEL: offset: 0, size: 8, alignment: 8
// ISEL: liveins: $r251, $r231, $r232, $r233, $r234, $r235, $r236, $r237, $r238, $r239, $r240, $r241, $r242, $r243, $r244, $r245, $r246
// ISEL: ADJCALLSTACKDOWN 24, 0
// ISEL: STOUI {{.*}}, {{%[0-9]+}}, 16
// ISEL: STOUI {{.*}}, {{%[0-9]+}}, 8
// ISEL: STOUI {{.*}}, {{%[0-9]+}}, 0
// ISEL: CALL_STATE @external_boundary{{.*}}implicit $r251{{.*}}implicit $r231{{.*}}implicit $r246

// ASM-LABEL: forward_direct:
// ASM: GETA r{{[0-9]+}}, %geta(external_direct)
// ASM: PUSHGO r31, r{{[0-9]+}}, 0
// ASM-LABEL: forward_boundary:
// ASM: STOU {{r[0-9]+}}, [[OUT:r[0-9]+]], 16
// ASM: STOU {{r[0-9]+}}, [[OUT]], 8
// ASM: STOU {{r[0-9]+}}, [[OUT]], 0
// ASM: GETA r{{[0-9]+}}, %geta(external_boundary)
// ASM: OR r251,
// ASM: PUSHGO r31, r{{[0-9]+}}, 0

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: R_MMIX_PUSHJ_STUBBABLE (36)
// ELF-NEXT: Symbol: external_direct
// ELF: Type: R_MMIX_PUSHJ_STUBBABLE (36)
// ELF-NEXT: Symbol: external_boundary
// ELF: Name: external_direct
// ELF: Section: Undefined
// ELF: Name: external_boundary
// ELF: Section: Undefined

// DIS-LABEL: <forward_direct>:
// DIS: PUSHJ r31, 0
// DIS-NEXT: {{.*}} R_MMIX_PUSHJ_STUBBABLE external_direct
// DIS-LABEL: <forward_boundary>:
// DIS: PUSHJ r31, 0
// DIS-NEXT: {{.*}} R_MMIX_PUSHJ_STUBBABLE external_boundary
