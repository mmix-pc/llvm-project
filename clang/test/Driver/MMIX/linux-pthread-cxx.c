// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: mkdir -p %t/root/usr/include/c++/v1 %t/root/usr/lib %t/resource/include %t/resource/lib/mmix-unknown-linux
// RUN: cp %t/profile %t/root/usr/lib/mmix-libc-profile
// These empty files test selection, not the package producer's archive audit.
// RUN: touch %t/root/usr/include/pthread.h %t/root/usr/lib/libc.a %t/root/usr/lib/crt1.o %t/root/usr/lib/libc++.a %t/root/usr/lib/libc++abi.a %t/root/usr/lib/libunwind.a %t/root/usr/lib/libm.a %t/root/usr/include/c++/v1/__config_site %t/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o %t/resource/lib/mmix-unknown-linux/clang_rt.crtend.o %t/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a %t/input.o
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=CC1
// RUN: %clangxx --target=mmix-unknown-linux-unknown --sysroot=%t/root -resource-dir %t/resource -pthread -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=CC1
// RUN: %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fno-cxx-exceptions -fexceptions -fno-unwind-tables -funwind-tables -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=CC1
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -flto=full -O2 -S -emit-llvm %t/input.c -o - | FileCheck %s --check-prefix=IR
// RUN: %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### %t/input.o 2>&1 | FileCheck %s --check-prefix=LINK --implicit-check-not=-lpthread --implicit-check-not=-lgcc
// RUN: %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### %t/input.o 2>&1 | FileCheck %s --check-prefix=C-LINK --implicit-check-not=libc++.a
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fno-exceptions -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fno-cxx-exceptions -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fno-unwind-tables -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fignore-exceptions -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fasynchronous-unwind-tables -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread --unwindlib=none -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fsjlj-exceptions -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fseh-exceptions -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -fwasm-exceptions -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -shared -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -ftls-model=initial-exec -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: mv %t/root/usr/lib/libc.a %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/root/usr/lib/libc.a
// RUN: mv %t/root/usr/lib/crt1.o %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/root/usr/lib/crt1.o
// RUN: mv %t/root/usr/lib/libc++.a %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/root/usr/lib/libc++.a
// RUN: mv %t/root/usr/lib/libc++abi.a %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/root/usr/lib/libc++abi.a
// RUN: mv %t/root/usr/lib/libunwind.a %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/root/usr/lib/libunwind.a
// RUN: mv %t/root/usr/lib/libm.a %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/root/usr/lib/libm.a
// RUN: mv %t/root/usr/include/c++/v1/__config_site %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/root/usr/include/c++/v1/__config_site
// RUN: mv %t/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o
// RUN: mv %t/resource/lib/mmix-unknown-linux/clang_rt.crtend.o %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/resource/lib/mmix-unknown-linux/clang_rt.crtend.o
// RUN: mv %t/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a %t/saved
// RUN: not %clang --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -nostdlib -L%t -B%t -### -c %t/input.c 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: mv %t/saved %t/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a
// RUN: cp %t/stale %t/root/usr/lib/mmix-libc-profile
// RUN: not %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### %t/input.o 2>&1 | FileCheck %s --check-prefix=STALE
// RUN: cp %t/wrong %t/root/usr/lib/mmix-libc-profile
// RUN: not %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### %t/input.o 2>&1 | FileCheck %s --check-prefix=WRONG
// RUN: rm %t/root/usr/lib/mmix-libc-profile
// RUN: not %clangxx --target=mmix-unknown-linux --sysroot=%t/root -resource-dir %t/resource -pthread -### %t/input.o 2>&1 | FileCheck %s --check-prefix=MISSING

// CC1: "-cc1"
// CC1-SAME: "-ftls-model=local-exec" "-fexceptions" "-funwind-tables=1"
// CC1-SAME: "-pthread"
// LINK: "-m" "elf64mmix_linux" "-static" "--no-dynamic-linker"
// LINK-SAME: "/{{.*}}/root/usr/lib/crt1.o" "/{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtbegin.o"
// LINK-SAME: "--start-group" "/{{.*}}/root/usr/lib/libc++.a" "/{{.*}}/root/usr/lib/libc++abi.a" "/{{.*}}/root/usr/lib/libm.a" "/{{.*}}/root/usr/lib/libunwind.a" "/{{.*}}/root/usr/lib/libc.a"
// LINK-SAME: "/{{.*}}/resource/lib/mmix-unknown-linux/libclang_rt.builtins.a" "--end-group"
// LINK-SAME: "/{{.*}}/resource/lib/mmix-unknown-linux/clang_rt.crtend.o"
// C-LINK: "--start-group" "/{{.*}}/root/usr/lib/libc++abi.a" "/{{.*}}/root/usr/lib/libunwind.a" "/{{.*}}/root/usr/lib/libc.a"
// IR: define {{.*}} @callback()
// IR: declare {{.*}}void @may_unwind() {{.*}}#[[DECL:[0-9]+]]
// IR-NOT: nounwind
// IR: attributes #[[DECL]] = {
// IR-NOT: nounwind
// REJECT: error: unsupported option
// MISSING: error: no such file or directory:
// STALE: error: the clang compiler does not support 'C++ or unwind runtime linking with MMIX Linux pthread'
// WRONG: error: the clang compiler does not support 'unrecognized MMIX Linux pthread libc profile'

//--- profile
mmix-linux-static-pthread-cxx-v1
//--- stale
mmix-linux-static-pthread-c-v1
//--- wrong
mmix-linux-static-pthread-cxx-v0
//--- input.c
void may_unwind(void);
void callback(void) { may_unwind(); }
