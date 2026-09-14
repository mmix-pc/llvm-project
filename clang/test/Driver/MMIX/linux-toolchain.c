// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix-unknown-linux -### -c %s 2>&1 | FileCheck %s --check-prefix=COMPILE --implicit-check-not="-internal-externc-isystem" --implicit-check-not="/usr/include" --implicit-check-not="mmix-unknown-unknown"
// RUN: %clang --target=mmix-unknown-linux-unknown -### -c %s 2>&1 | FileCheck %s --check-prefix=ALIAS
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t.sysroot -nostdlibinc -### -c %s 2>&1 | FileCheck %s --check-prefix=COMPILE --implicit-check-not="/usr/include"
// RUN: %clang --target=mmix-unknown-linux -nostdinc -### -c %s 2>&1 | FileCheck %s --check-prefix=COMPILE --implicit-check-not="-internal-isystem"
// RUN: %clang --target=mmix-unknown-linux -nobuiltininc -### -c %s 2>&1 | FileCheck %s --check-prefix=COMPILE --implicit-check-not="-internal-isystem"
// RUN: %clang --target=mmix-unknown-linux -nostdlibinc -### -c %s 2>&1 | FileCheck %s --check-prefix=BUILTIN
// RUN: %clang --target=mmix-unknown-linux -fPIC -fno-pic -### -c %s 2>&1 | FileCheck %s --check-prefix=COMPILE
// RUN: %clang --target=mmix-unknown-linux --cstdlib=llvm-libc --rtlib=compiler-rt -stdlib=libc++ --unwindlib=libunwind -fuse-ld=lld -x c++ -### -c %s 2>&1 | FileCheck %s --check-prefix=COMPILE
// RUN: %clang --target=mmix-unknown-linux -E %s -o %t.i
// RUN: %clang --target=mmix-unknown-linux -fsyntax-only %s
// RUN: %clang --target=mmix-unknown-linux -S %s -o %t.s
// RUN: %clang --target=mmix-unknown-linux -c %s -o %t.o
// RUN: %clang --target=mmix-unknown-linux -x c++ -c %s -o %t.cxx.o
// RUN: %clang --target=mmix-unknown-linux -c %t.s -o %t.asm.o
// RUN: llvm-readobj --file-headers %t.o %t.cxx.o %t.asm.o | FileCheck %s --check-prefix=ELF
// RUN: not %clang --target=mmix-unknown-linux  -### %t.o 2>&1 | FileCheck %s --check-prefix=LINK --implicit-check-not='"-cc1"' --implicit-check-not='"ld"' --implicit-check-not='"ld.lld"'
// RUN: not %clang --target=mmix-unknown-linux -static -### %t.o 2>&1 | FileCheck %s --check-prefix=LINK --implicit-check-not='"-cc1"' --implicit-check-not='"ld"' --implicit-check-not='"ld.lld"'
// RUN: %clang --target=mmix-unknown-linux -r -nostdlib -### %t.o 2>&1 | FileCheck %s --check-prefix=LINK-RELOC
// RUN: %clang --target=mmix-unknown-linux -nostartfiles -nodefaultlibs -### %t.o 2>&1 | FileCheck %s --check-prefix=LINK-STATIC
// RUN: not %clang --target=mmix-unknown-linux -fuse-ld=lld -### %t.o 2>&1 | FileCheck %s --check-prefix=LINK --implicit-check-not='"-cc1"' --implicit-check-not='"ld"' --implicit-check-not='"ld.lld"'
// RUN: not %clang --target=mmix-unknown-linux -fno-integrated-as -### -c %s 2>&1 | FileCheck %s --check-prefix=ASSEMBLER
// RUN: not %clang --target=mmix-unknown-linux -shared -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -pie -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -static-pie -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -fPIC -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -fPIE -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -pthread -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --cstdlib=newlib -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --cstdlib=system -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --rtlib=libgcc -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -stdlib=libstdc++ -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --unwindlib=libgcc -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -fuse-ld=bfd -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --gcc-toolchain=%t.gcc -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --gcc-install-dir=%t.gcc -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --gcc-triple=mmix-unknown-linux-gnu -### -c %s 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %clang --target=mmix-unknown-unknown -nostdlib -r -### %t.o 2>&1 | FileCheck %s --check-prefix=BARE

// COMPILE: "-cc1" "-triple" "mmix-unknown-linux"
// COMPILE-SAME: "-mrelocation-model" "static"
// ALIAS: "-cc1" "-triple" "mmix-unknown-linux-unknown"
// ALIAS-SAME: "-mrelocation-model" "static"
// BUILTIN: "-internal-isystem" "{{.*}}/include"
// ELF-COUNT-3: Format: elf64-mmix
// LINK: error: the clang compiler does not support 'implicit system resources without --sysroot for MMIX Linux'
// LINK-RELOC: "-m" "elf64mmix_linux" "-r"
// LINK-STATIC: "-m" "elf64mmix_linux" "-static" "--no-dynamic-linker"
// ASSEMBLER: error: the clang compiler does not support 'external assembly for MMIX Linux'
// REJECT: error: unsupported option '{{.*}}' for target 'mmix-unknown-linux'
// BARE: "{{.*}}ld.lld{{.*}}"

// No sysroot is needed for freestanding use of builtin headers.
#include <stddef.h>
long add(long a, long b) { return a + b; }
