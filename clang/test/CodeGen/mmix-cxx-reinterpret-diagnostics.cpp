// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_QUALIFIER %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=QUALIFIER
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_REFERENCE %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REFERENCE
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_NARROW_INTEGER %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NARROW
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_ADDRESS_SPACE %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ADDRESS-SPACE

struct Source {
  long value;
};

struct Target {
  unsigned long value;
};

#if defined(TEST_QUALIFIER)
Target *remove_const(const Source *value) {
  return reinterpret_cast<Target *>(value);
}
// QUALIFIER: error: reinterpret_cast from 'const Source *' to 'Target *' casts away qualifiers
#elif defined(TEST_REFERENCE)
Target &bind_prvalue() { return reinterpret_cast<Target &>(Source{1}); }
// REFERENCE: error: reinterpret_cast from rvalue to reference type 'Target &'
#elif defined(TEST_NARROW_INTEGER)
unsigned int lose_pointer(void *value) {
  return reinterpret_cast<unsigned int>(value);
}
// NARROW: error: cast from pointer to smaller type 'unsigned int' loses information
#elif defined(TEST_ADDRESS_SPACE)
using AS1Source = Source __attribute__((address_space(1)));
Target *change_address_space(AS1Source *value) {
  return reinterpret_cast<Target *>(value);
}
// ADDRESS-SPACE: error: reinterpret_cast from 'AS1Source *' {{.*}}to 'Target *' is not allowed
#endif
