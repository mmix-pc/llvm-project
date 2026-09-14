// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: mkdir -p %t/root/usr/lib %t/resource/lib/mmix-unknown-linux
// RUN: touch %t/input.o %t/root/usr/lib/crt1.o %t/root/usr/lib/libc.a %t/root/usr/lib/libm.a %t/root/usr/lib/libc++.a %t/root/usr/lib/libc++abi.a %t/root/usr/lib/libunwind.a
// RUN: touch %t/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o %t/resource/lib/mmix-unknown-linux/clang_rt.crtend.o %t/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -### %t/input.o -o %t/a.out 2>&1 | FileCheck %s --check-prefix=C --implicit-check-not=crti.o --implicit-check-not=crtn.o --implicit-check-not=libgcc --implicit-check-not=mmix-qemu
// RUN: %clang --target=mmix-unknown-linux-unknown --sysroot=%t/root -resource-dir %t/resource -static -### %t/input.o -o %t/a.out 2>&1 | FileCheck %s --check-prefix=C
// RUN: %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -### %t/input.o 2>&1 | FileCheck %s --check-prefix=CXX
// RUN: %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -fno-exceptions -### %t/input.o 2>&1 | FileCheck %s --check-prefix=CXX
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -fexceptions -### %t/input.o 2>&1 | FileCheck %s --check-prefix=UNWIND
// RUN: %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource --unwindlib=none -### %t/input.o 2>&1 | FileCheck %s --check-prefix=DEFAULTS --implicit-check-not=libunwind
// RUN: %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -nostdlib++ --unwindlib=none -### %t/input.o 2>&1 | FileCheck %s --check-prefix=DEFAULTS --implicit-check-not=libc++ --implicit-check-not=libm.a
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/missing -resource-dir %t/missing-resource -nostdlib -### %t/input.o 2>&1 | FileCheck %s --check-prefix=RAW --implicit-check-not=crt --implicit-check-not=libc.a --implicit-check-not=libclang_rt
// RUN: %clang --target=mmix-unknown-linux -nostdlib -### %t/input.o -Wl,--gc-sections -Xlinker --strip-debug -L%t/extra -T%t/script 2>&1 | FileCheck %s --check-prefix=FORWARD
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -nodefaultlibs -### %t/input.o 2>&1 | FileCheck %s --check-prefix=START --implicit-check-not=libc.a --implicit-check-not=builtins --implicit-check-not=--start-group
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -nostartfiles -### %t/input.o 2>&1 | FileCheck %s --check-prefix=DEFAULTS --implicit-check-not=crt1.o --implicit-check-not=crtbegin --implicit-check-not=crtend
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -nolibc -### %t/input.o 2>&1 | FileCheck %s --check-prefix=START --implicit-check-not=libc.a
// RUN: %clangxx --target=mmix-unknown-linux --sysroot=%t/missing -resource-dir %t/missing-resource -r -### %t/input.o 2>&1 | FileCheck %s --check-prefix=RELOC --implicit-check-not=crt --implicit-check-not=libc.a --implicit-check-not=libclang_rt --implicit-check-not=-static
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/missing -resource-dir %t/resource -### %t/input.o 2>&1 | FileCheck %s --check-prefix=MISSING-CRT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/missing -resource-dir %t/resource -nostartfiles -L%t/root/usr/lib -B%t/root/usr/lib -### %t/input.o 2>&1 | FileCheck %s --check-prefix=MISSING-LIBC
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/missing-resource -### %t/input.o 2>&1 | FileCheck %s --check-prefix=MISSING-RT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -shared -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -pie -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -static-pie -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -Wl,-shared -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -Xlinker --dynamic-linker=/bad/ld.so -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -Wl,-Bdynamic -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -Wl,-r -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -Wl,-m,elf_x86_64 -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -Wl,-m,elf64mmix -### %t/input.o 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux -nostdlib -flto -### %t/input.o 2>&1 | FileCheck %s --check-prefix=LTO

// Empty fixtures establish command composition, not Linux runtime validity.
// C: "{{.*}}ld.lld{{.*}}" "-m" "elf64mmix_linux" "-static" "--no-dynamic-linker" "--eh-frame-hdr"
// C-SAME: "-z" "max-page-size=8192" "-z" "common-page-size=8192"
// C-SAME: "{{.*}}/root/usr/lib/crt1.o" "{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o" "{{.*}}/input.o" "--start-group" "{{.*}}/root/usr/lib/libc.a" "{{.*}}/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a" "--end-group" "{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtend.o" "-o" "{{.*}}/a.out"
// CXX: "--start-group" "{{.*}}/root/usr/lib/libc++.a" "{{.*}}/root/usr/lib/libc++abi.a" "{{.*}}/root/usr/lib/libm.a" "{{.*}}/root/usr/lib/libunwind.a" "{{.*}}/root/usr/lib/libc.a" "{{.*}}/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a" "--end-group"
// UNWIND: "--start-group" "{{.*}}/root/usr/lib/libunwind.a" "{{.*}}/root/usr/lib/libc.a"
// DEFAULTS: "--start-group"
// DEFAULTS-SAME: "{{.*}}/root/usr/lib/libc.a"
// DEFAULTS-SAME: "{{.*}}/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a" "--end-group"
// START: "{{.*}}/root/usr/lib/crt1.o" "{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o"
// START-SAME: "{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtend.o"
// RAW: "-m" "elf64mmix_linux" "-static"
// RAW-SAME: "{{.*}}/input.o"
// FORWARD: "-L{{.*}}/extra"
// FORWARD-SAME: "--gc-sections" "--strip-debug"
// FORWARD-SAME: "-T" "{{.*}}/script"
// RELOC: "-m" "elf64mmix_linux" "-r"
// RELOC-SAME: "{{.*}}/input.o"
// MISSING-CRT: error: no such file or directory: '{{.*}}/missing/usr/lib/crt1.o'
// MISSING-LIBC: error: no such file or directory: '{{.*}}/missing/usr/lib/libc.a'
// MISSING-RT: error: no such file or directory: '{{.*}}/missing-resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o'
// REJECT: error: unsupported option
// LTO: error: the clang compiler does not support 'LTO linking for MMIX Linux'
//--- script
//--- input.ll
define void @_start() {
  ret void
}
