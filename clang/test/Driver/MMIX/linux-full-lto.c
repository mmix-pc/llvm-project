// REQUIRES: mmix-registered-target, lld
// RUN: split-file %s %t
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -O2 -flto=full -c %t/entry.c -o %t/entry.bc
// RUN: %clang --target=mmix-unknown-linux-unknown -ffreestanding -O2 -flto=full -c %t/provider.c -o %t/provider.bc
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -O2 -c %t/native.c -o %t/native.o
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -flto=full -c %t/unused.c -o %t/unused.bc
// RUN: llvm-ar rcs %t/provider.a %t/unused.bc %t/provider.bc
// RUN: %clang --target=mmix-unknown-linux -nostdlib -flto=full -O0 %t/entry.bc %t/provider.a %t/native.o -Wl,-e,_start,--threads=1,--trace -o %t/o0 2>&1 | FileCheck %s --check-prefix=TRACE --implicit-check-not=unused.bc
// RUN: %clang --target=mmix-unknown-linux -nostdlib -flto=full -O2 %t/entry.bc %t/provider.a %t/native.o -Wl,-e,_start,--threads=1 -o %t/o2
// RUN: %clang --target=mmix-unknown-linux-unknown -nostdlib -O2 %t/entry.bc %t/provider.bc %t/native.o -Wl,-e,_start,--threads=1 -o %t/explicit
// RUN: %clang --target=mmix-unknown-linux -nostdlib -O2 %t/entry.c %t/provider.a %t/native.o -Wl,-e,_start,--threads=1 -o %t/mixed
// RUN: llvm-readobj -h -r --symbols %t/o0 | FileCheck %s --check-prefix=ELF --implicit-check-not=missing --implicit-check-not=unused
// RUN: llvm-readobj -h -r --symbols %t/o2 | FileCheck %s --check-prefix=ELF --implicit-check-not=missing --implicit-check-not=unused
// RUN: llvm-readobj -h -r --symbols %t/explicit | FileCheck %s --check-prefix=ELF --implicit-check-not=missing --implicit-check-not=unused
// RUN: llvm-readobj -h -r --symbols %t/mixed | FileCheck %s --check-prefix=ELF --implicit-check-not=missing --implicit-check-not=unused
// RUN: %clang --target=mmix-unknown-linux -nostdlib -flto -O2 -### %t/entry.bc 2>&1 | FileCheck %s --check-prefix=OPTIONS --implicit-check-not=LLVMgold --implicit-check-not=thinlto --implicit-check-not=libgcc
// RUN: %clang --target=mmix-unknown-linux -nostdlib -flto=thin -flto=full -O2 -### %t/entry.bc 2>&1 | FileCheck %s --check-prefix=OPTIONS
// RUN: %clang --target=mmix-unknown-linux -nostdlib -O2 -### %t/entry.bc 2>&1 | FileCheck %s --check-prefix=NO-LTO --implicit-check-not=plugin-opt
// RUN: %clang --target=mmix-unknown-linux -nostdlib -flto=thin -fno-lto -### %t/native.o 2>&1 | FileCheck %s --check-prefix=NO-LTO --implicit-check-not=plugin-opt
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -flto=full -flto=thin -### %t/entry.bc 2>&1 | FileCheck %s --check-prefix=THIN
// RUN: not %clang --target=mmix-unknown-linux -r -flto=full -### %t/native.o 2>&1 | FileCheck %s --check-prefix=PARTIAL
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -flto=full -Wl,-plugin,bad.so -### %t/entry.bc 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -flto=full --ld-path=bad -### %t/entry.bc 2>&1 | FileCheck %s --check-prefix=CUSTOM
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -flto=full -shared -### %t/entry.bc 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -flto=full -pie -### %t/entry.bc 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -flto=full -O2 %t/entry.bc -Wl,-e,_start,--threads=1 -o %t/missing 2>&1 | FileCheck %s --check-prefix=MISSING

// TRACE: provider.a({{.*}}provider.bc)
// ELF: Format: elf64-mmix
// ELF: Type: Executable
// ELF: Machine: EM_MMIX
// ELF: Relocations [
// ELF-NEXT: ]
// ELF-DAG: Name: _start
// ELF-DAG: Name: native_leaf
// OPTIONS: "{{.*}}ld.lld{{.*}}" "-m" "elf64mmix_linux" "-static" "--no-dynamic-linker" "--eh-frame-hdr"
// OPTIONS-SAME: "-plugin-opt=O2"
// NO-LTO: "{{.*}}ld.lld{{.*}}" "-m" "elf64mmix_linux" "-static"
// THIN: error: the clang compiler does not support 'ThinLTO linking for MMIX Linux'
// PARTIAL: error: the clang compiler does not support 'LTO relocatable linking for MMIX Linux'
// REJECT: error: unsupported option
// CUSTOM: error: the clang compiler does not support 'custom linker selection for MMIX Linux'
// MISSING: undefined symbol: provider

//--- entry.c
extern long provider(void);
long _start(void) { return provider(); }
//--- provider.c
extern long native_leaf(void);
long provider(void) { return native_leaf() + 7; }
//--- native.c
long native_leaf(void) { return 42; }
//--- unused.c
extern long missing(void);
long unused(void) { return missing(); }
