// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: mkdir -p %t/root/usr/include %t/root/usr/lib %t/resource/include %t/resource/lib/mmix-unknown-linux
// RUN: cp %t/profile %t/root/usr/lib/mmix-libc-profile
// RUN: touch %t/root/usr/include/pthread.h %t/root/usr/lib/libc.a %t/root/usr/lib/crt1.o %t/input.o
// RUN: touch %t/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a %t/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o %t/resource/lib/mmix-unknown-linux/clang_rt.crtend.o
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=COMPILE
// RUN: %clang --target=mmix-unknown-linux-unknown --sysroot=%t/root -resource-dir %t/resource -pthread -E -dM %t/input.c | FileCheck %s --check-prefix=MACRO
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### %t/input.o 2>&1 | FileCheck %s --check-prefix=LINK --implicit-check-not=-lpthread --implicit-check-not=mmix_thread_lifecycle --implicit-check-not=libunwind --implicit-check-not=libc++
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -flto=full -### %t/input.o 2>&1 | FileCheck %s --check-prefix=LINK
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -shared -### %t/input.o 2>&1 | FileCheck %s --check-prefix=MODE
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -ftls-model=global-dynamic -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MODE
// RUN: not %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### %t/input.o 2>&1 | FileCheck %s --check-prefix=UNWIND
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fexceptions -### %t/input.o 2>&1 | FileCheck %s --check-prefix=UNWIND
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/absent -resource-dir %t/resource -pthread -L%t/root/usr/lib -B%t/root/usr/lib -### %t/input.o 2>&1 | FileCheck %s --check-prefix=PROFILE
// RUN: mv %t/root/usr/include/pthread.h %t/saved-header
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=HEADER
// RUN: mv %t/saved-header %t/root/usr/include/pthread.h
// RUN: mv %t/root/usr/lib/crt1.o %t/saved-crt
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -### %t/input.o 2>&1 | FileCheck %s --check-prefix=CRT
// RUN: mv %t/saved-crt %t/root/usr/lib/crt1.o
// RUN: mv %t/root/usr/lib/libc.a %t/saved-libc
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=LIBC
// RUN: mv %t/saved-libc %t/root/usr/lib/libc.a
// RUN: cp %t/wrong-profile %t/root/usr/lib/mmix-libc-profile
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: rm %t/root/usr/lib/mmix-libc-profile
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=ORDINARY --implicit-check-not='"-pthread"'

// These empty resources test Driver selection only, not runtime validity.
// COMPILE: "-cc1"
// COMPILE-SAME: "-ftls-model=local-exec"
// COMPILE-SAME: "-internal-externc-isystem" "{{.*}}/root/usr/include"
// COMPILE-SAME: "-pthread"
// MACRO: #define _REENTRANT 1
// LINK: "{{.*}}ld.lld{{.*}}" "-m" "elf64mmix_linux" "-static" "--no-dynamic-linker"
// LINK-SAME: "{{.*}}/root/usr/lib/crt1.o" "{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o"
// LINK-SAME: "--start-group" "{{.*}}/root/usr/lib/libc.a" "{{.*}}/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a" "--end-group"
// LINK-SAME: "{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtend.o"
// MODE: error: unsupported option
// UNWIND: error: the clang compiler does not support 'C++ or unwind runtime linking with MMIX Linux pthread'
// PROFILE: error: no such file or directory: '{{.*}}/absent/usr/lib/mmix-libc-profile'
// HEADER: error: no such file or directory: '{{.*}}/root/usr/include/pthread.h'
// CRT: error: no such file or directory: '{{.*}}/root/usr/lib/crt1.o'
// LIBC: error: no such file or directory: '{{.*}}/root/usr/lib/libc.a'
// INVALID: error: the clang compiler does not support 'unrecognized MMIX Linux pthread libc profile'
// ORDINARY: "-cc1"

//--- profile
mmix-linux-static-pthread-c-v1
//--- wrong-profile
mmix-linux-single
//--- input.c
int value;
