// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix-unknown-linux -static -fasynchronous-unwind-tables -S -emit-llvm %s -o - | FileCheck %s --check-prefix=ASYNC
// RUN: %clang --target=mmix-unknown-linux -static -funwind-tables -fno-asynchronous-unwind-tables -S -emit-llvm %s -o - | FileCheck %s --check-prefix=SYNC
// RUN: %clang --target=mmix-unknown-linux -static -fasynchronous-unwind-tables -fno-asynchronous-unwind-tables -fno-unwind-tables -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NONE
// RUN: %clang --target=mmix-unknown-linux -static -fno-asynchronous-unwind-tables -fasynchronous-unwind-tables -c %s -o %t.o
// RUN: llvm-readobj --sections %t.o | FileCheck %s --check-prefix=OBJECT
// RUN: %clang --target=mmix-unknown-linux -static -### -c %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// RUN: not %clang --target=mmix-unknown-unknown -fasynchronous-unwind-tables -### -c %s 2>&1 | FileCheck %s --check-prefix=GENERIC

// ASYNC: attributes #{{[0-9]+}} = { {{.*}}uwtable{{( |\()}}
// ASYNC-NOT: uwtable(sync)
// SYNC: uwtable(sync)
// NONE-NOT: uwtable
// DEFAULT-NOT: "-funwind-tables=2"
// DEFAULT: "-cc1"
// DEFAULT-NOT: "-funwind-tables=2"
// OBJECT: Name: .eh_frame
// GENERIC: error: unsupported option '-fasynchronous-unwind-tables'
extern void callee(void);
void caller(void) { callee(); }
