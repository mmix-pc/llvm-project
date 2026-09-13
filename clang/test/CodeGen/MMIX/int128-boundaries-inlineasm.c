// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify -DIMMEDIATE %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify -DIMMEDIATE %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify -DIMMEDIATE %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify -DIMMEDIATE %s
// RUN: not %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -DVECTOR %s 2>&1 | FileCheck %s --check-prefix=VECTOR --implicit-check-not="Stack dump" --implicit-check-not="PLEASE submit"
// RUN: not %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -DVECTOR %s 2>&1 | FileCheck %s --check-prefix=VECTOR --implicit-check-not="Stack dump" --implicit-check-not="PLEASE submit"
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -DVECTOR %s 2>&1 | FileCheck %s --check-prefix=VECTOR --implicit-check-not="Stack dump" --implicit-check-not="PLEASE submit"
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -DVECTOR %s 2>&1 | FileCheck %s --check-prefix=VECTOR --implicit-check-not="Stack dump" --implicit-check-not="PLEASE submit"

#ifdef IMMEDIATE
// A wide constant whose low word fits must not be truncated into an immediate.
void immediate(void) {
  __asm__ volatile("SWYM %0" : : "I"(((__int128)1 << 100) + 1)); // expected-error {{out of range for constraint 'I'}}
}
#endif
#ifdef VECTOR
// Even an otherwise supported octa-sized vector remains excluded from asm.
typedef int i32x2 __attribute__((vector_size(8)));
__int128 vector_asm(__int128 value) {
  i32x2 lanes = {1, 2};
  __asm__ volatile("" : "+r"(lanes));
  return value + lanes[0];
}
// VECTOR: MMIX inline assembly does not support vector operands
#endif
