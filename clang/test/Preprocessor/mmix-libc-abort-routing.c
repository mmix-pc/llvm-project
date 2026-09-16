// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mmix-unknown-linux -E -P -I %t -I %S/../../../libc %t/dispatch.c | FileCheck %s --check-prefix=MMIX-LINUX
// RUN: %clang_cc1 -triple mmix-unknown-unknown -E -P -I %t -I %S/../../../libc %t/dispatch.c | FileCheck %s --check-prefix=MMIX
// RUN: %clang_cc1 -triple x86_64-unknown-linux -E -P -I %t -I %S/../../../libc %t/dispatch.c | FileCheck %s --check-prefix=LINUX
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -E -P -I %t -I %S/../../../libc %t/dispatch.c | FileCheck %s --check-prefix=GENERIC

// Test the real dispatcher with marker leaves, not complete abort providers.
// LINUX: linux_abort
// MMIX-LINUX: mmix_linux_abort
// MMIX: mmix_baremetal_abort
// GENERIC: generic_baremetal_abort
//--- dispatch.c
#include "src/stdlib/abort_utils.h"
//--- src/stdlib/linux/abort_utils.h
linux_abort
//--- src/stdlib/linux/mmix/abort_utils.h
mmix_linux_abort
//--- src/stdlib/baremetal/mmix/abort_utils.h
mmix_baremetal_abort
//--- src/stdlib/baremetal/abort_utils.h
generic_baremetal_abort
