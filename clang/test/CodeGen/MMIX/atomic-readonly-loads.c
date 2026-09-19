// RUN: %clang --target=mmix-unknown-linux -O0 -S %s -o - | FileCheck %s --implicit-check-not=CSWAP
// RUN: %clang --target=mmix-unknown-linux -O2 -S %s -o - | FileCheck %s --implicit-check-not=CSWAP
// RUN: %clang --target=mmix-unknown-unknown -O2 -S %s -o - | FileCheck %s --implicit-check-not=CSWAP

// These loads must work for read-only memory regardless of the stored value.

// CHECK-LABEL: load_byte:
// CHECK: LDBU {{r[0-9]+}}, {{r[0-9]+}}, 0
unsigned char load_byte(const unsigned char *p) {
  return __atomic_load_n(p, __ATOMIC_RELAXED);
}

// CHECK-LABEL: load_wyde:
// CHECK: LDWU {{r[0-9]+}}, {{r[0-9]+}}, 0
unsigned short load_wyde(const unsigned short *p) {
  return __atomic_load_n(p, __ATOMIC_RELAXED);
}

// CHECK-LABEL: load_tetra:
// CHECK: LDTU {{r[0-9]+}}, {{r[0-9]+}}, 0
unsigned int load_tetra(const unsigned int *p) {
  return __atomic_load_n(p, __ATOMIC_RELAXED);
}

// CHECK-LABEL: load_octa:
// CHECK: LDOU {{r[0-9]+}}, {{r[0-9]+}}, 0
unsigned long load_octa(const unsigned long *p) {
  return __atomic_load_n(p, __ATOMIC_RELAXED);
}
