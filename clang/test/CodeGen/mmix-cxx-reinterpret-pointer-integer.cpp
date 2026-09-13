// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O2 \
// RUN:   -mrelocation-model static -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_NARROW_INTEGER %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NARROW
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_ADDRESS_SPACE %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ADDRESS-SPACE
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_CONSTANT_EXPRESSION %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CONSTANT

using uintptr_t = __UINTPTR_TYPE__;

#if defined(TEST_NARROW_INTEGER)
unsigned int too_narrow(void *value) {
  return reinterpret_cast<unsigned int>(value);
}
// NARROW: error: cast from pointer to smaller type 'unsigned int' loses information
#elif defined(TEST_ADDRESS_SPACE)
using AS1Int = int __attribute__((address_space(1)));
uintptr_t unsupported_address_space(AS1Int *value) {
  return reinterpret_cast<uintptr_t>(value);
}
// ADDRESS-SPACE: error: MMIX GNU ABI does not support argument type 'AS1Int *'
#elif defined(TEST_CONSTANT_EXPRESSION)
int object;
constexpr uintptr_t invalid_constant = reinterpret_cast<uintptr_t>(&object);
// CONSTANT: error: constexpr variable 'invalid_constant' must be initialized by a constant expression
// CONSTANT: note: cast that performs the conversions of a reinterpret_cast is not
// CONSTANT-SAME: allowed in a constant expression
#else
uintptr_t pointer_to_integer(void *value) {
  return reinterpret_cast<uintptr_t>(value);
}

unsigned long long pointer_to_other_wide_integer(void *value) {
  return reinterpret_cast<unsigned long long>(value);
}

void *integer_to_pointer(uintptr_t value) {
  return reinterpret_cast<void *>(value);
}

void *round_trip(void *value) {
  return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(value));
}

// IR-LABEL: define {{.*}} @_Z18pointer_to_integerPv(
// IR: ptrtoint ptr {{%.*}} to i64
// IR-LABEL: define {{.*}} @_Z29pointer_to_other_wide_integerPv(
// IR: ptrtoint ptr {{%.*}} to i64
// IR-LABEL: define {{.*}} @_Z18integer_to_pointerm(
// IR: inttoptr i64 {{%.*}} to ptr
// IR-LABEL: define {{.*}} @_Z10round_tripPv(
// IR: ret ptr {{%.*}}
#endif
