// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -emit-obj -o %t.o %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -O2 -emit-obj -o %t.opt.o %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -mrelocation-model static -emit-obj -o %t.generic.o %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=BITS

typedef char bytes32 __attribute__((ext_vector_type(32)));
typedef int words4 __attribute__((ext_vector_type(4)));
typedef _Bool mask1 __attribute__((ext_vector_type(1)));
typedef _Bool mask2 __attribute__((ext_vector_type(2)));
typedef _Bool mask4 __attribute__((ext_vector_type(4)));
typedef short halves16 __attribute__((ext_vector_type(16)));
typedef long octas4 __attribute__((ext_vector_type(4)));
typedef float singles8 __attribute__((ext_vector_type(8)));
typedef double doubles4 __attribute__((ext_vector_type(4)));

_Static_assert(sizeof(mask1) == 1 && _Alignof(mask1) == 1, "byte storage");
_Static_assert(sizeof(bytes32) == 32 && _Alignof(bytes32) == 32, "wide storage");

// CHECK-LABEL: define dso_local void @pass_bytes(
// CHECK-SAME: ptr {{[^,]*}}sret(<32 x i8>) align 32
// CHECK-SAME: , ptr {{[^,]*}}align 32
bytes32 pass_bytes(bytes32 v) { return v; }
// CHECK-LABEL: define dso_local void @pass_words(
// CHECK-SAME: ptr {{[^,]*}}sret(<4 x i32>) align 16
words4 pass_words(words4 v) { return v; }

bytes32 forward(bytes32 v) { return pass_bytes(v); }
// CHECK-LABEL: define dso_local i8 @pass_mask1(i8 noext noundef
mask1 pass_mask1(mask1 v) { return v; }
mask2 pass_mask2(mask2 v) { return v; }
mask4 pass_mask4(mask4 v) { return v; }

// Partial masks occupy the high bits of their big-endian storage byte.
// BITS-LABEL: define{{.*}} i8 @first_bit(
// BITS: ret i8 -128
unsigned char first_bit(void) {
  return __builtin_bit_cast(unsigned char, (mask4){1, 0, 0, 0}) & 0xf0;
}
// BITS-LABEL: define{{.*}} i8 @last_bit(
// BITS: ret i8 16
unsigned char last_bit(void) {
  return __builtin_bit_cast(unsigned char, (mask4){0, 0, 0, 1}) & 0xf0;
}

int contains(bytes32 v, char c) { return __builtin_reduce_or(v == (bytes32)c); }
mask1 convert(char c) {
  typedef char byte1 __attribute__((ext_vector_type(1)));
  return __builtin_convertvector((byte1)c, mask1);
}

halves16 add_halves(halves16 a, halves16 b) { return a + b; }
octas4 add_octas(octas4 a, octas4 b) { return a + b; }
singles8 add_singles(singles8 a, singles8 b) { return a + b; }
doubles4 multiply_doubles(doubles4 a, doubles4 b) { return a * b; }
