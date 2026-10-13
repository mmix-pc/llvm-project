// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix-unknown-linux -fpic -### -c %s 2>&1 | FileCheck %s --check-prefix=SMALL
// RUN: %clang --target=mmix-unknown-linux -fPIC -### -c %s 2>&1 | FileCheck %s --check-prefix=LARGE
// RUN: %clang --target=mmix-unknown-linux -fpie -### -c %s 2>&1 | FileCheck %s --check-prefixes=SMALL,PIE
// RUN: %clang --target=mmix-unknown-linux -fPIE -### -c %s 2>&1 | FileCheck %s --check-prefixes=LARGE,PIE
// RUN: %clang --target=mmix-unknown-linux -fPIC -fno-pic -### -c %s 2>&1 | FileCheck %s --check-prefix=STATIC
// RUN: %clang --target=mmix-unknown-linux -fpic -fno-PIC -### -c %s 2>&1 | FileCheck %s --check-prefix=STATIC
// RUN: %clang --target=mmix-unknown-linux -fPIE -fno-pie -### -c %s 2>&1 | FileCheck %s --check-prefix=STATIC
// RUN: %clang --target=mmix-unknown-linux -fpie -fno-PIE -### -c %s 2>&1 | FileCheck %s --check-prefix=STATIC
// RUN: %clang --target=mmix-unknown-linux -fno-pic -fPIC -### -c %s 2>&1 | FileCheck %s --check-prefix=LARGE
// RUN: %clang --target=mmix-unknown-linux -fPIC -fpie -### -c %s 2>&1 | FileCheck %s --check-prefixes=SMALL,PIE
// RUN: %clang --target=mmix-unknown-linux -fPIE -fpic -### -c %s 2>&1 | FileCheck %s --check-prefix=SMALL --implicit-check-not=pic-is-pie
// RUN: %clang --target=mmix-unknown-linux -### -c %s 2>&1 | FileCheck %s --check-prefix=STATIC
// RUN: %clang --target=mmix-unknown-linux -fpic -E -dM %s | FileCheck %s --check-prefix=MACRO-SMALL --implicit-check-not=__PIE__
// RUN: %clang --target=mmix-unknown-linux -fPIC -E -dM %s | FileCheck %s --check-prefix=MACRO-LARGE --implicit-check-not=__PIE__
// RUN: %clang --target=mmix-unknown-linux -fpie -E -dM %s | FileCheck %s --check-prefixes=MACRO-SMALL,MACRO-PIE-SMALL
// RUN: %clang --target=mmix-unknown-linux -fPIE -E -dM %s | FileCheck %s --check-prefixes=MACRO-LARGE,MACRO-PIE-LARGE
// RUN: not %clang --target=mmix-unknown-linux -fPIC -shared -### %s 2>&1 | FileCheck %s --check-prefix=RUNTIME
// RUN: not %clang --target=mmix-unknown-linux -fPIE -pie -### %s 2>&1 | FileCheck %s --check-prefix=RUNTIME
// SMALL: "-mrelocation-model" "pic"
// SMALL-SAME: "-pic-level" "1"
// LARGE: "-mrelocation-model" "pic"
// LARGE-SAME: "-pic-level" "2"
// PIE-SAME: "-pic-is-pie"
// STATIC: "-mrelocation-model" "static"
// STATIC-NOT: "-pic-level"
// MACRO-SMALL: #define __PIC__ 1
// MACRO-LARGE: #define __PIC__ 2
// MACRO-PIE-SMALL: #define __PIE__ 1
// MACRO-PIE-LARGE: #define __PIE__ 2
// RUNTIME: error: the clang compiler does not support 'MMIX Linux dynamic CRT and runtime providers

extern long value;
long *address(void) { return &value; }
