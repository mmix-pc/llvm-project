// RUN: %clang -### --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/../../CodeGen/mmix-abi-backend-integration.c \
// RUN:   -o %t.ll 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TEXT-JOB \
// RUN:       --implicit-check-not=-cc1as --implicit-check-not=-emit-obj \
// RUN:       --implicit-check-not=llvm-dis
// RUN: %clang -### --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -c -emit-llvm -fdiscard-value-names %S/../../CodeGen/mmix-abi-backend-integration.c \
// RUN:   -o %t.bc 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BC-JOB \
// RUN:       --implicit-check-not=-cc1as --implicit-check-not=-emit-obj \
// RUN:       --implicit-check-not=llvm-dis

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -S -emit-llvm -fdiscard-value-names %S/../../CodeGen/mmix-abi-backend-integration.c \
// RUN:   -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -c -emit-llvm -fdiscard-value-names %S/../../CodeGen/mmix-abi-backend-integration.c \
// RUN:   -o %t.bc
// RUN: llvm-dis %t.bc -o - | FileCheck %s --check-prefix=IR

// TEXT-JOB: (in-process)
// TEXT-JOB-NEXT: {{.*}}clang{{.*}} "-cc1" "-triple" "mmix-unknown-unknown"
// TEXT-JOB-SAME: "-emit-llvm"
// TEXT-JOB-SAME: "-mrelocation-model" "static"
// TEXT-JOB-SAME: "-ffreestanding"
// TEXT-JOB-SAME: "-std=gnu2x"
// TEXT-JOB-SAME: "-o" "{{.*}}.ll"
// TEXT-JOB-SAME: "-x" "c" "{{.*}}mmix-abi-backend-integration.c"

// BC-JOB: (in-process)
// BC-JOB-NEXT: {{.*}}clang{{.*}} "-cc1" "-triple" "mmix-unknown-unknown"
// BC-JOB-SAME: "-emit-llvm-bc"
// BC-JOB-SAME: "-mrelocation-model" "static"
// BC-JOB-SAME: "-ffreestanding"
// BC-JOB-SAME: "-std=gnu2x"
// BC-JOB-SAME: "-o" "{{.*}}.bc"
// BC-JOB-SAME: "-x" "c" "{{.*}}mmix-abi-backend-integration.c"

// IR: target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
// IR-NEXT: target triple = "mmix-unknown-unknown"
// IR: %struct.Large = type { [3 x i64] }
// IR: %struct.Direct = type { i32 }
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
// IR: getelementptr inbounds i8, ptr %{{[0-9]+}}, i64 8
// IR: call void @llvm.va_end.p0(
// IR: call i64 @recursive_sum(i64 noundef
// IR: call i64 %{{[0-9]+}}(i64 noundef
// IR: call i64 @external_scalar(i8 noundef signext
// IR-SAME: i16 noundef zeroext
// IR: call i32 @external_direct(i32 noext
// IR-SAME: ptr noundef byval(%struct.Large) align 8
// IR: call i64 (i64, ...) @external_variadic(i64 noundef
// IR-SAME: i32 noext
// IR-SAME: ptr noundef byval(%struct.Large) align 8
// IR: call void @llvm.memcpy.p0.p0.i64(ptr align 8
// IR-SAME: i64 24, i1 false)

// IR: attributes #{{[0-9]+}} = {
// IR-SAME: "target-features"="+base,+cache,+system,+virtual-memory"
