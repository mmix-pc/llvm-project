// REQUIRES: mmix-registered-target, lld
// RUN: %clang --target=mmix-unknown-linux -nostdlib -rdynamic -### %s 2>&1 | FileCheck %s --check-prefix=DRIVER
// RUN: %clang --target=mmix-unknown-linux -nostdlib -### %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang --target=mmix-unknown-linux -static -nostdlib -rdynamic -### %s 2>&1 | FileCheck %s --check-prefix=DRIVER
// RUN: %clang --target=mmix-unknown-linux -shared -nostdlib -rdynamic -### %s 2>&1 | FileCheck %s --check-prefix=DRIVER
// RUN: %clang --target=mmix-unknown-linux -fno-unwind-tables -fno-asynchronous-unwind-tables -c %s -o %t.o
// RUN: %clang --target=mmix-unknown-linux -nostdlib -Wl,-e,entry %t.o -o %t.default
// RUN: llvm-readobj --dyn-symbols %t.default | FileCheck %s --check-prefix=NOEXPORT
// RUN: %clang --target=mmix-unknown-linux -nostdlib -rdynamic -Wl,-e,entry %t.o -o %t.pie
// RUN: llvm-readobj --dyn-symbols %t.pie | FileCheck %s --check-prefix=EXPORT
// RUN: %clang --target=mmix-unknown-linux -no-pie -nostdlib -rdynamic -Wl,-e,entry %t.o -o %t.exec
// RUN: llvm-readobj --dyn-symbols %t.exec | FileCheck %s --check-prefix=EXPORT
// RUN: %clang --target=mmix-unknown-linux -no-pie -nostdlib -rdynamic -flto -Wl,--gc-sections,-e,entry %s -o %t.lto
// RUN: llvm-readobj --dyn-symbols %t.lto | FileCheck %s --check-prefix=EXPORT
// DRIVER: "{{.*}}ld.lld"
// DRIVER-SAME: "--export-dynamic"
// DRIVER-NOT: unused
// DEFAULT: "{{.*}}ld.lld"
// DEFAULT-NOT: --export-dynamic
// NOEXPORT: DynamicSymbols [
// NOEXPORT-NOT: Name: exported
// EXPORT: DynamicSymbols [
// EXPORT-NOT: Name: hidden
// EXPORT: Name: exported
// EXPORT-NOT: Name: hidden

// No CRT: this tests symbol export independently of startup visibility policy.
long exported = 123;
__attribute__((visibility("hidden"))) long hidden = 456;
void entry(void) {}
