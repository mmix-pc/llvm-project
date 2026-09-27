// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix-unknown-linux -### -c %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang --target=mmix-unknown-linux-unknown -### -c %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang --target=mmix-unknown-unknown -### -c %s 2>&1 | FileCheck %s --check-prefix=GENERIC
// RUN: %clang --target=mmix-unknown-linux -ftls-model=global-dynamic -ftls-model=local-exec -### -c %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// RUN: not %clang --target=mmix-unknown-linux -ftls-model=local-exec -ftls-model=global-dynamic -### -c %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang --target=mmix-unknown-linux -ftls-model=initial-exec -### -c %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang --target=mmix-unknown-linux -ftls-model=local-dynamic -### -c %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang --target=mmix-unknown-linux -femulated-tls -### -c %s 2>&1 | FileCheck %s --check-prefix=EMULATED
// RUN: %clang --target=mmix-unknown-linux -femulated-tls -fno-emulated-tls -### -c %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// RUN: not %clang --target=mmix-unknown-linux -fno-emulated-tls -femulated-tls -### -c %s 2>&1 | FileCheck %s --check-prefix=EMULATED
// RUN: not %clang --target=mmix-unknown-linux -fPIC -### -c %s 2>&1 | FileCheck %s --check-prefix=PIC
// RUN: not %clang --target=mmix-unknown-linux -fPIE -### -c %s 2>&1 | FileCheck %s --check-prefix=PIC
// RUN: not %clang --target=mmix-unknown-linux -shared -### %s 2>&1 | FileCheck %s --check-prefix=SHARED
// RUN: not %clang --target=mmix-unknown-linux -pthread -### -c %s 2>&1 | FileCheck %s --check-prefix=THREAD
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -O0 -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -O2 -c %s -o %t.o
// RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=OBJ

// DEFAULT: "-cc1"
// DEFAULT-SAME: "-ftls-model=local-exec"
// DEFAULT-NOT: "-femulated-tls"
// GENERIC: "-cc1"
// GENERIC-NOT: "-ftls-model=local-exec"
// MODEL: error: unsupported option '-ftls-model={{.*}}' for target 'mmix-unknown-linux'
// EMULATED: error: unsupported option '-femulated-tls'
// PIC: error: unsupported option '-fP{{IC|IE}}'
// SHARED: error: unsupported option '-shared'
// THREAD: error: unsupported option '-pthread'
// IR: @value = external thread_local(localexec) global i64
// OBJ: R_MMIX_TPREL_LO16 value
// OBJ-NEXT: R_MMIX_TPREL_ML16 value
// OBJ-NEXT: R_MMIX_TPREL_MH16 value
// OBJ-NEXT: R_MMIX_TPREL_HI16 value

extern _Thread_local long value;
long read_value(void) { return value; }
