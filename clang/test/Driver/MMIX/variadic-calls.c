// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/Inputs/variadic-calls.c -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: FileCheck %s --check-prefix=NO-RAW < %t.ll
// RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
// RUN:   -stop-after=mmix-isel %t.ll -o - | FileCheck %s --check-prefix=ISEL

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S %S/Inputs/variadic-calls.c -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --implicit-check-not=MMIXAL < %t.s

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -c %S/Inputs/variadic-calls.c -o %t.o
// RUN: llvm-readobj --file-headers --symbols --relocations --expand-relocs \
// RUN:   %t.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// NO-RAW-NOT: va_arg

// IR-LABEL: define dso_local i64 @consume_variadic(
// IR-SAME: i64 noundef %{{[0-9]+}}, ...)
// IR: [[AP:%[0-9]+]] = alloca ptr, align 8
// IR: [[COPY:%[0-9]+]] = alloca ptr, align 8
// IR: call void @llvm.va_start.p0(ptr [[AP]])
// IR: call void @llvm.va_copy.p0(ptr [[COPY]], ptr [[AP]])
// The promoted int and direct aggregate are right-adjusted in their octas.
// IR: [[INT_CUR:%[0-9]+]] = load ptr, ptr [[AP]], align 8
// IR: getelementptr inbounds i8, ptr [[INT_CUR]], i64 8
// IR: getelementptr inbounds i8, ptr [[INT_CUR]], i64 4
// IR: load i32,
// IR: [[DOUBLE_CUR:%[0-9]+]] = load ptr, ptr [[AP]], align 8
// IR: getelementptr inbounds i8, ptr [[DOUBLE_CUR]], i64 8
// IR: load double, ptr [[DOUBLE_CUR]], align 8
// IR: [[DIRECT_CUR:%[0-9]+]] = load ptr, ptr [[AP]], align 8
// IR: getelementptr inbounds i8, ptr [[DIRECT_CUR]], i64 8
// IR: getelementptr inbounds i8, ptr [[DIRECT_CUR]], i64 4
// IR: call void @llvm.memcpy.p0.p0.i64({{.*}}i64 4, i1 false)
// The caller-copy aggregate slot contains a pointer to the 24-byte object.
// IR: [[LARGE_CUR:%[0-9]+]] = load ptr, ptr [[AP]], align 8
// IR: getelementptr inbounds i8, ptr [[LARGE_CUR]], i64 8
// IR: load ptr, ptr [[LARGE_CUR]], align 8
// IR: call void @llvm.memcpy.p0.p0.i64({{.*}}i64 24, i1 false)
// IR: load ptr, ptr [[COPY]], align 8
// IR: call void @llvm.va_end.p0(ptr [[COPY]])
// IR: call void @llvm.va_end.p0(ptr [[AP]])

// Default argument promotions and aggregate classification are visible at
// the public C call site.
// IR-LABEL: define dso_local i64 @call_variadic(
// IR: sext i8 %{{[0-9]+}} to i32
// IR: fpext float %{{[0-9]+}} to double
// IR: call i64 ({{.*}}) @consume_variadic({{.*}}i32 noundef signext %{{[0-9]+}}, double noundef %{{[0-9]+}}, i32 noext %{{[0-9]+}}, ptr noundef byval(%struct.Large) align 8 %{{[0-9]+}})

// The first unnamed argument arrives in r246 and is saved before the cursor
// walks continuously into the caller-provided stack slots.
// ISEL-LABEL: name: consume_variadic
// ISEL: [[REGARG:%[0-9]+]]:{{[^ ]+}} = COPY $r246
// ISEL: STOUI [[REGARG]], %fixed-stack.0, 0
// ISEL: ADDUI %fixed-stack.0, 0
// ISEL: nuw ADDUI {{%[0-9]+}}, 8
// ISEL: LDTUI {{%[0-9]+}}, 4
// ISEL: LDOUI {{%[0-9]+}}, 0
// ISEL: LDOUI {{%[0-9]+}}, 0
// ISEL: LDOUI {{%[0-9]+}}, 16
// ISEL-LABEL: name: call_variadic
// ISEL: ADJCALLSTACKDOWN 24, 0
// ISEL: STOUI {{.*}}, {{%[0-9]+}}, 16
// ISEL: STOUI {{.*}}, {{%[0-9]+}}, 8
// ISEL: STOUI {{.*}}, {{%[0-9]+}}, 0
// ISEL: CALL_STATE @consume_variadic{{.*}}implicit $r231{{.*}}implicit $r245, implicit $r246

// ASM-LABEL: consume_variadic:
// ASM: STOU r246,
// ASM: LDTU {{r[0-9]+}}, {{r[0-9]+}}, 4
// ASM: LDOU {{r[0-9]+}}, {{r[0-9]+}}, 0
// ASM-LABEL: call_variadic:
// ASM: STOU {{r[0-9]+}}, [[OUT:r[0-9]+]], 16
// ASM: STOU {{r[0-9]+}}, [[OUT]], 8
// ASM: STOU {{r[0-9]+}}, [[OUT]], 0
// ASM: PUSHJB r31, consume_variadic

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: R_MMIX_PUSHJ_STUBBABLE (36)
// ELF-NEXT: Symbol: consume_variadic
// ELF: Name: consume_variadic
// ELF: Type: Function
// ELF: Name: call_variadic
// ELF: Type: Function

// DIS-LABEL: <consume_variadic>:
// DIS: STOU r246,
// DIS: LDTU {{r[0-9]+}}, {{r[0-9]+}}, 4
// DIS-LABEL: <call_variadic>:
// DIS: PUSHJB r31,
// DIS-NEXT: {{.*}} R_MMIX_PUSHJ_STUBBABLE consume_variadic
