// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=mmix-unknown-linux -O0 -S -o %t.O0.s %s
// RUN: FileCheck %s --check-prefix=ASM < %t.O0.s
// RUN: %clang --target=mmix-unknown-linux -O2 -S -o %t.O2.s %s
// RUN: FileCheck %s --check-prefix=ASM < %t.O2.s
// RUN: %clang --target=mmix-unknown-linux -c %S/../../../../libc/src/__support/OSUtil/linux/mmix/syscall.S -o %t.o
// RUN: not %clang --target=mmix-unknown-unknown -c %S/../../../../libc/src/__support/OSUtil/linux/mmix/syscall.S -o %t.bad.o 2>&1 | FileCheck %s --check-prefix=OS

#include "../../../../libc/src/__support/OSUtil/linux/mmix/syscall.h"

// OS: error: "The MMIX Linux syscall adapter requires MMIX Linux"
// IR-LABEL: define{{.*}} @full_width(
// IR: call i64 @__llvm_libc_mmix_syscall(i64{{.*}} 81985529216486895, i64{{.*}} 1229782938247303441, i64{{.*}} 2459565876494606882, i64{{.*}} 3689348814741910323, i64{{.*}} 4919131752989213764, i64{{.*}} 6148914691236517205, i64{{.*}} -7378697629483820647)
// ASM-LABEL: full_width:
// ASM: GET [[RETURN:r[0-9]+]], rJ
// ASM: GETA [[CALLEE:r[0-9]+]], %geta(__llvm_libc_mmix_syscall)
// ASM: PUSHGO r31, [[CALLEE]], 0
// ASM: PUT rJ, [[RETURN]]
// ASM: POP 0, 0
long full_width(void) {
  return __llvm_libc_mmix_syscall(0x0123456789abcdefUL,
      0x1111111111111111L, 0x2222222222222222L, 0x3333333333333333L,
      0x4444444444444444L, 0x5555555555555555L, -0x6666666666666667L) + 1;
}

// Live values exceed the allocatable local bank and require preservation
// across an ordinary call. The volatile accesses also bracket syscall effects.
// IR-LABEL: define{{.*}} @pressure(
// IR: store volatile i64
// IR: call i64 @__llvm_libc_mmix_syscall(
// IR: load volatile i64
// ASM-LABEL: pressure:
// ASM: GET [[PRESSURE_RETURN:r[0-9]+]], rJ
// ASM: STOU
// ASM: GETA [[PRESSURE_CALLEE:r[0-9]+]], %geta(__llvm_libc_mmix_syscall)
// ASM: PUSHGO r31, [[PRESSURE_CALLEE]], 0
// ASM: LDOU
// ASM: PUT rJ, [[PRESSURE_RETURN]]
// ASM: POP 0, 0
long pressure(volatile long *p) {
#define LOAD(n) long v##n = p[n]
  LOAD(0); LOAD(1); LOAD(2); LOAD(3); LOAD(4); LOAD(5); LOAD(6); LOAD(7);
  LOAD(8); LOAD(9); LOAD(10); LOAD(11); LOAD(12); LOAD(13); LOAD(14); LOAD(15);
  LOAD(16); LOAD(17); LOAD(18); LOAD(19); LOAD(20); LOAD(21); LOAD(22); LOAD(23);
  LOAD(24); LOAD(25); LOAD(26); LOAD(27); LOAD(28); LOAD(29); LOAD(30); LOAD(31);
#undef LOAD
  p[32] = 123;
  long result = __llvm_libc_mmix_syscall(0xfedcba9876543210UL,
                                       v0, v1, v2, v3, v4, v5);
#define STORE(n) p[n] = v##n
  STORE(0); STORE(1); STORE(2); STORE(3); STORE(4); STORE(5); STORE(6); STORE(7);
  STORE(8); STORE(9); STORE(10); STORE(11); STORE(12); STORE(13); STORE(14); STORE(15);
  STORE(16); STORE(17); STORE(18); STORE(19); STORE(20); STORE(21); STORE(22); STORE(23);
  STORE(24); STORE(25); STORE(26); STORE(27); STORE(28); STORE(29); STORE(30); STORE(31);
#undef STORE
  return result + p[33];
}
