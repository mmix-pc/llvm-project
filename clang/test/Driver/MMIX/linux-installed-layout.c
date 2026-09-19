// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: mkdir -p %t/sysroots/linux/usr/lib %t/override/usr/lib %t/resource/lib/mmix-unknown-linux
// RUN: touch %t/sysroots/linux/usr/lib/crt1.o %t/sysroots/linux/usr/lib/libc.a %t/sysroots/linux/usr/lib/libc++.a %t/sysroots/linux/usr/lib/libc++abi.a %t/sysroots/linux/usr/lib/libm.a %t/sysroots/linux/usr/lib/libunwind.a
// RUN: touch %t/override/usr/lib/crt1.o %t/override/usr/lib/libc.a
// RUN: touch %t/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o %t/resource/lib/mmix-unknown-linux/clang_rt.crtend.o %t/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a
// RUN: %clang --config=%t/linux.cfg -### %t/input.o 2>&1 | FileCheck %s --check-prefix=C
// RUN: %clangxx --config=%t/linux.cfg -### %t/input.o 2>&1 | FileCheck %s --check-prefix=CXX
// RUN: %clang --config=%t/linux.cfg --target=mmix-unknown-linux-unknown -### %t/input.o 2>&1 | FileCheck %s --check-prefix=C
// RUN: %clang --config=%t/linux.cfg --sysroot=%t/override -### %t/input.o 2>&1 | FileCheck %s --check-prefix=OVERRIDE --implicit-check-not=sysroots/linux/usr/lib
// RUN: %clang --config=%t/linux.cfg -fexceptions -### %t/input.o 2>&1 | FileCheck %s --check-prefix=EXCEPTIONS
// RUN: %clang --config=%t/linux.cfg -nostdlib -### %t/input.o 2>&1 | FileCheck %s --check-prefix=LINK --implicit-check-not=crt1.o --implicit-check-not=libc.a --implicit-check-not=libclang_rt
// RUN: %clang --config=%t/linux.cfg -nostartfiles -### %t/input.o 2>&1 | FileCheck %s --check-prefix=LINK --implicit-check-not=crt1.o --implicit-check-not=crtbegin --implicit-check-not=crtend
// RUN: %clang --config=%t/linux.cfg -nodefaultlibs -### %t/input.o 2>&1 | FileCheck %s --check-prefix=LINK --implicit-check-not=libc.a --implicit-check-not=libclang_rt
// RUN: not %clang --config=%t/linux.cfg --sysroot=%t/missing -### %t/input.o 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not %clang --no-default-config --target=mmix-unknown-unknown -resource-dir %t/resource -print-libgcc-file-name 2>&1 | FileCheck %s --check-prefix=GENERIC --implicit-check-not=lib/mmix-unknown-linux/

// Path fixtures test Driver ownership and order, not runtime linkability.
// C: "-m" "elf64mmix_linux"
// C-SAME: "{{.*}}/sysroots/linux/usr/lib/crt1.o"
// C-SAME: "{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o"
// C-SAME: "{{.*}}/input.o" "--start-group" "{{.*}}/sysroots/linux/usr/lib/libc.a"
// C-SAME: "{{.*}}/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a" "--end-group"
// C-SAME: "{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtend.o"
// CXX: "--start-group" "{{.*}}/sysroots/linux/usr/lib/libc++.a" "{{.*}}/sysroots/linux/usr/lib/libc++abi.a" "{{.*}}/sysroots/linux/usr/lib/libm.a" "{{.*}}/sysroots/linux/usr/lib/libunwind.a" "{{.*}}/sysroots/linux/usr/lib/libc.a"
// OVERRIDE: "{{.*}}/override/usr/lib/crt1.o"
// OVERRIDE-SAME: "{{.*}}/override/usr/lib/libc.a"
// EXCEPTIONS: "--start-group" "{{.*}}/sysroots/linux/usr/lib/libunwind.a" "{{.*}}/sysroots/linux/usr/lib/libc.a"
// LINK: "-m" "elf64mmix_linux"
// MISSING: error: no such file or directory: '{{.*}}/missing/usr/lib/crt1.o'
// GENERIC: error: no such file or directory: '{{.*}}/resource/lib/mmix-unknown-unknown/libclang_rt.builtins.a'

//--- linux.cfg
--target=mmix-unknown-linux
--sysroot=<CFGDIR>/sysroots/linux
-resource-dir=<CFGDIR>/resource
//--- input.o
