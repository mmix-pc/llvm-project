// REQUIRES: mmix-registered-target, lld
// RUN: %clang --target=mmix-unknown-linux -g -gdwarf-4 -O0 -fPIC -shared -nostdlib %s -o %t.v4.so
// RUN: llvm-dwarfdump --verify %t.v4.so
// RUN: llvm-dwarfdump --debug-info %t.v4.so | FileCheck %s --check-prefix=DWARF4
// RUN: %clang --target=mmix-unknown-linux -g -gdwarf-5 -O2 -fPIC -shared -nostdlib %s -o %t.v5.so
// RUN: llvm-dwarfdump --verify %t.v5.so
// RUN: llvm-dwarfdump --debug-info %t.v5.so | FileCheck %s --check-prefix=DWARF5
// RUN: %clang --target=mmix-unknown-linux -g -gdwarf-4 -O2 -static -nostdlib %s -o %t.static
// RUN: llvm-dwarfdump --verify %t.static
// RUN: llvm-dwarfdump --debug-info %t.static | FileCheck %s --check-prefix=DWARF4
// DWARF4: DW_OP_const8u 0x0, DW_OP_GNU_push_tls_address
// DWARF4: DW_OP_const8u 0x8, DW_OP_GNU_push_tls_address
// DWARF5: DW_OP_const8u 0x0, DW_OP_GNU_push_tls_address
// DWARF5: DW_OP_const8u 0x8, DW_OP_GNU_push_tls_address

_Thread_local long initialized = 42;
_Thread_local long zero;
void _start(void) {}
