// RUN: %clang --target=mmix-unknown-linux -O0 -ffreestanding -fno-jump-tables -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %clang --target=mmix-unknown-linux -O2 -ffreestanding -fno-jump-tables -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %clang --target=mmix-unknown-linux -O0 -ffreestanding -fno-jump-tables -mllvm -verify-machineinstrs -S %s -o - | FileCheck %s
// RUN: %clang --target=mmix-unknown-linux -O2 -ffreestanding -fno-jump-tables -mllvm -verify-machineinstrs -S %s -o - | FileCheck %s
// RUN: %clang --target=mmix-unknown-linux -O0 -ffreestanding -fno-jump-tables -c %s -o %t.o
// RUN: %clang --target=mmix-unknown-linux -O2 -ffreestanding -fno-jump-tables -c %s -o %t.o

// Explicit computed goto must work even with automatic jump tables disabled.
// IR: blockaddress(@dispatch,
// IR: indirectbr ptr
// CHECK-LABEL: dispatch:
// CHECK: GO r255, {{r[0-9]+}}, 0
unsigned long dispatch(unsigned long op, unsigned long value) {
  static void *const labels[] = {&&add, &&subtract, &&done};
  goto *labels[op % 3];
add:
  return value + 7;
subtract:
  return value - 9;
done:
  return value;
}

// Sixty-four volatile loads precede the transfer, with each value used in
// either successor. This exceeds the allocatable register set and exercises
// spill/reload and frame preservation without introducing a procedure call.
#define VALUES(M) \
  M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) \
  M(8) M(9) M(10) M(11) M(12) M(13) M(14) M(15) \
  M(16) M(17) M(18) M(19) M(20) M(21) M(22) M(23) \
  M(24) M(25) M(26) M(27) M(28) M(29) M(30) M(31) \
  M(32) M(33) M(34) M(35) M(36) M(37) M(38) M(39) \
  M(40) M(41) M(42) M(43) M(44) M(45) M(46) M(47) \
  M(48) M(49) M(50) M(51) M(52) M(53) M(54) M(55) \
  M(56) M(57) M(58) M(59) M(60) M(61) M(62) M(63)
#define LOAD(N) unsigned long v##N = p[N];
#define ADD(N) out[N] = v##N + delta;
#define SUB(N) out[N] = v##N - delta;

// CHECK-LABEL: pressure:
// CHECK-DAG: STOU
// CHECK-DAG: GO r255, {{r[0-9]+}}, 0
// CHECK-DAG: LDOU
unsigned long pressure(volatile unsigned long *p, volatile unsigned long *out,
                       unsigned long op, unsigned long delta) {
  static void *const labels[] = {&&add, &&subtract};
  VALUES(LOAD)
  goto *labels[op & 1];
add:
  VALUES(ADD)
  return delta;
subtract:
  VALUES(SUB)
  return delta;
}
