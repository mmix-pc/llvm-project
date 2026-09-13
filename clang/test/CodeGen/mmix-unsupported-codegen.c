// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_ATOMIC_BUILTIN %s 2>&1 | FileCheck %s --check-prefix=BUILTIN
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_VECTOR %s 2>&1 | FileCheck %s --check-prefix=VECTOR
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_WIDE_OPERATION %s 2>&1 | FileCheck %s --check-prefix=WIDE
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_BITINT %s 2>&1 | FileCheck %s --check-prefix=BITINT
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_COMPLEX_INTEGER_OPERATION %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=COMPLEX-INTEGER
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_ADDRESS_SPACE %s 2>&1 | FileCheck %s --check-prefix=AS
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_NAKED %s 2>&1 | FileCheck %s --check-prefix=NAKED
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_TARGET %s 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -o - \
// RUN:   -DTEST_SUPPORTED_BUILTINS %s | FileCheck %s --check-prefix=SUPPORTED

#if defined(TEST_ATOMIC_BUILTIN)
float fetch_add(float *value) {
  return __atomic_fetch_add(value, 1.0f, __ATOMIC_SEQ_CST);
}
// BUILTIN: error: MMIX GNU ABI does not support atomic operation __atomic_fetch_add
#elif defined(TEST_VECTOR)
typedef int int4 __attribute__((ext_vector_type(4)));
int4 value;
// VECTOR: error: MMIX GNU ABI does not support vector value CodeGen involving type 'int4'
#elif defined(TEST_WIDE_OPERATION)
long multiply(long value) {
  _BitInt(128) wide = value;
  wide *= wide;
  return (long)wide;
}
// WIDE: error: signed _BitInt of bit sizes greater than 64 not supported
#elif defined(TEST_BITINT)
_BitInt(17) add_bitint(_BitInt(17) lhs, _BitInt(17) rhs) {
  return lhs + rhs;
}
// BITINT: error: MMIX GNU ABI does not support extended scalar operation CodeGen involving type '_BitInt(17)'
#elif defined(TEST_COMPLEX_INTEGER_OPERATION)
_Complex int add_complex_integer(_Complex int lhs, _Complex int rhs) {
  return lhs + rhs;
}
// COMPLEX-INTEGER: error: MMIX GNU ABI does not support return type '_Complex int'
// COMPLEX-INTEGER: error: MMIX GNU ABI does not support argument type '_Complex int'
// COMPLEX-INTEGER: error: MMIX GNU ABI does not support extended scalar operation CodeGen involving type '_Complex int'
#elif defined(TEST_ADDRESS_SPACE)
int __attribute__((address_space(1))) value;
// AS: error: MMIX GNU ABI does not support nonzero-address-space value CodeGen involving type '__attribute__((address_space(1))) int'
#elif defined(TEST_NAKED)
__attribute__((naked)) void unsupported(void) {}
// NAKED: error: MMIX does not support the 'naked' function attribute
#elif defined(TEST_TARGET)
__attribute__((target("base"))) void unsupported(void) {}
// TARGET: error: MMIX does not support the 'target' function attribute
#elif defined(TEST_SUPPORTED_BUILTINS)
void copy_eight(char *destination, const char *source) {
  __builtin_memcpy(destination, source, 8);
}

double fused(double a, double b, double c) {
  return __builtin_fma(a, b, c);
}

// SUPPORTED-LABEL: define dso_local void @copy_eight(
// SUPPORTED: call void @llvm.memcpy
// SUPPORTED-LABEL: define dso_local double @fused(
// SUPPORTED: call double @llvm.fma.f64(
#endif
