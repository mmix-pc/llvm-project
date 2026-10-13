// REQUIRES: mmix-registered-target, lld
// RUN: split-file %s %t
// RUN: %clang --target=mmix-unknown-linux -c %t/provider.s -o %t/provider.o
// RUN: %clang --target=mmix-unknown-linux -c %t/main.s -o %t/main.o
// RUN: %clang --target=mmix-unknown-linux -shared -nostdlib %t/provider.o -Wl,-soname,libprovider.so -o %t/provider.so
// RUN: %clang --target=mmix-unknown-linux -pie -nostdlib -e entry %t/main.o %t/provider.so -o %t/pie
// RUN: %clang --target=mmix-unknown-linux -no-pie -Wl,-Bdynamic -nostdlib -e entry %t/main.o %t/provider.so -o %t/exec
// RUN: %clang --target=mmix-unknown-linux -no-pie -Xlinker -Bdynamic -nostdlib -Wl,-e,entry %t/main.o %t/provider.so -o %t/exec-xlinker
// RUN: llvm-readobj -h --program-headers --dynamic-table %t/provider.so | FileCheck %s --check-prefix=DSO --implicit-check-not=PT_INTERP
// RUN: llvm-readobj -h --program-headers --dynamic-table %t/pie | FileCheck %s --check-prefixes=PIE,MAIN
// RUN: llvm-readobj -h --program-headers --dynamic-table %t/exec %t/exec-xlinker | FileCheck %s --check-prefixes=EXEC,MAIN
// RUN: llvm-readobj --string-dump=.interp %t/pie %t/exec %t/exec-xlinker | FileCheck %s --check-prefix=INTERP
// RUN: %clang --target=mmix-unknown-linux -pie -nostdlib -e entry -### %t/main.o %t/provider.so 2>&1 | FileCheck %s --check-prefix=COMMAND --implicit-check-not=crt --implicit-check-not=libclang_rt --implicit-check-not=libc.a
// RUN: %clang --target=mmix-unknown-linux -pie -no-pie -nostdlib -### %t/main.o 2>&1 | FileCheck %s --check-prefix=STATIC
// RUN: %clang --target=mmix-unknown-linux -static -nostdlib -### %t/main.o 2>&1 | FileCheck %s --check-prefix=STATIC
// RUN: %clang --target=mmix-unknown-linux -no-pie -nostdlib -### %t/main.o 2>&1 | FileCheck %s --check-prefix=STATIC
// RUN: not %clang --target=mmix-unknown-linux -shared -static -nostdlib -### %t/main.o 2>&1 | FileCheck %s --check-prefix=CONFLICT
// RUN: not %clang --target=mmix-unknown-linux -shared -pie -nostdlib -### %t/main.o 2>&1 | FileCheck %s --check-prefix=CONFLICT
// RUN: not %clang --target=mmix-unknown-linux -pie -r -nostdlib -### %t/main.o 2>&1 | FileCheck %s --check-prefix=CONFLICT
// RUN: not %clang --target=mmix-unknown-linux -Wl,-Bdynamic -nostdlib -### %t/main.o 2>&1 | FileCheck %s --check-prefix=CONFLICT
// RUN: not %clang --target=mmix-unknown-linux -pie -nostdlib -Wl,--no-dynamic-linker -### %t/main.o 2>&1 | FileCheck %s --check-prefix=FORWARD
// RUN: not %clang --target=mmix-unknown-linux -pie -nostdlib -Wl,--dynamic-linker=/bad -### %t/main.o 2>&1 | FileCheck %s --check-prefix=FORWARD
// RUN: not %clang --target=mmix-unknown-linux -pie -nostdlib -Wl,-Bstatic -### %t/main.o 2>&1 | FileCheck %s --check-prefix=FORWARD
// RUN: not %clang --target=mmix-unknown-linux -shared -nostdlib -Wl,-pie -### %t/main.o 2>&1 | FileCheck %s --check-prefix=FORWARD
// RUN: not %clang --target=mmix-unknown-linux -shared -### %t/provider.o 2>&1 | FileCheck %s --check-prefix=PROVIDER
// RUN: not %clang --target=mmix-unknown-linux -pie -nostartfiles -### %t/main.o 2>&1 | FileCheck %s --check-prefix=PROVIDER
// RUN: not %clang --target=mmix-unknown-linux -pie -nodefaultlibs -### %t/main.o 2>&1 | FileCheck %s --check-prefix=PROVIDER
// RUN: %clang --target=mmix-unknown-linux -pie -nostartfiles -nodefaultlibs -e entry %t/main.o %t/provider.so -o %t/no-defaults
// RUN: %clang --target=mmix-unknown-linux -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables -shared -nostdlib %t/consumer.c %t/provider.so -o %t/consumer.so
// RUN: llvm-readobj -r %t/consumer.so | FileCheck %s --check-prefix=GOT
// RUN: %clang --target=mmix-unknown-linux -shared -nostdlib -Wl,-z,lazy %t/provider.o -o %t/lazy
// RUN: llvm-readobj --dynamic-table %t/lazy | FileCheck %s --check-prefix=EAGER
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/missing -resource-dir %t/missing-resource -shared -nostdlib -### %t/provider.o 2>&1 | FileCheck %s --check-prefix=RAW --implicit-check-not=crt1 --implicit-check-not=libclang_rt
// RUN: not %clang --target=mmix-unknown-linux -pie -nostdlib -e missing %t/main.o %t/provider.so -o %t/bad 2>&1 | FileCheck %s --check-prefix=ENTRY
// RUN: not %clang --target=mmix-unknown-linux -pie -nostdlib -e entry %t/main.o -o %t/missing 2>&1 | FileCheck %s --check-prefix=UNDEFINED
// RUN: not %clang --target=mmix-unknown-unknown -shared -nostdlib -### %t/provider.o 2>&1 | FileCheck %s --check-prefix=GENERIC
// DSO: Type: SharedObject
// DSO: SONAME{{.*}}libprovider.so
// DSO: FLAGS{{.*}}BIND_NOW
// PIE: Type: SharedObject
// EXEC: Type: Executable
// MAIN: PT_INTERP
// INTERP-COUNT-3: /lib/ld-llvm-mmix.so.1
// MAIN: NEEDED{{.*}}libprovider.so
// MAIN: FLAGS{{.*}}BIND_NOW
// COMMAND: "-m" "elf64mmix_linux" "-pie" "--dynamic-linker=/lib/ld-llvm-mmix.so.1"
// COMMAND-SAME: "-z" "now" "-z" "relro" "-z" "text"
// COMMAND-SAME: "-e" "entry"
// STATIC: "-m" "elf64mmix_linux" "-static" "--no-dynamic-linker"
// CONFLICT: error: unsupported option 'conflicting MMIX Linux link modes'
// FORWARD: error: unsupported option
// PROVIDER: error: the clang compiler does not support 'MMIX Linux dynamic CRT and runtime providers
// ENTRY: MMIX Linux requires a defined entry symbol or address
// UNDEFINED: undefined symbol: external
// GENERIC: error: the clang compiler does not support 'shared linking for MMIX'
// GOT: R_MMIX_GLOB_DAT external 0x0
// EAGER: FLAGS{{.*}}BIND_NOW
// RAW: "-m" "elf64mmix_linux" "-shared"

//--- provider.s
.data
.p2align 3
.globl external
.type external,@object
external: .quad 123
//--- main.s
.text
.p2align 3
.globl entry
.type entry,@function
entry:
SWYM 0,0,0
SWYM 0,0,0
.mmix_got_pc64 external
//--- consumer.c
extern long external;
long *address(void) { return &external; }
