// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_OVERALIGNED_ARGUMENT_DEFINITION %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OVERALIGNED-ARGUMENT-DEFINITION
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_OVERALIGNED_ARGUMENT_CALL %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OVERALIGNED-ARGUMENT-CALL
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_OVERALIGNED_RESULT_DEFINITION %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OVERALIGNED-RESULT-DEFINITION
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_OVERALIGNED_RESULT_CALL %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OVERALIGNED-RESULT-CALL
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_UNSUPPORTED_FIELD %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNSUPPORTED-FIELD

#if defined(TEST_OVERALIGNED_ARGUMENT_DEFINITION) ||                         \
    defined(TEST_OVERALIGNED_ARGUMENT_CALL) ||                              \
    defined(TEST_OVERALIGNED_RESULT_DEFINITION) ||                          \
    defined(TEST_OVERALIGNED_RESULT_CALL)
struct alignas(16) OverAligned {
  long Value;
};

#if defined(TEST_OVERALIGNED_ARGUMENT_DEFINITION)
long read(OverAligned Value) { return Value.Value; }
// OVERALIGNED-ARGUMENT-DEFINITION: error: MMIX GNU ABI does not support over-aligned aggregate argument type 'OverAligned'
#elif defined(TEST_OVERALIGNED_ARGUMENT_CALL)
long read(OverAligned Value);
long call_read(OverAligned *Value) { return read(*Value); }
// OVERALIGNED-ARGUMENT-CALL: error: MMIX GNU ABI does not support over-aligned aggregate argument type 'OverAligned'
#elif defined(TEST_OVERALIGNED_RESULT_DEFINITION)
OverAligned make(long Value) { return {Value}; }
// OVERALIGNED-RESULT-DEFINITION: error: MMIX GNU ABI does not support over-aligned aggregate return type 'OverAligned'
#elif defined(TEST_OVERALIGNED_RESULT_CALL)
OverAligned make(long Value);
void call_make(long Value) { (void)make(Value); }
// OVERALIGNED-RESULT-CALL: error: MMIX GNU ABI does not support over-aligned aggregate return type 'OverAligned'
#endif

#elif defined(TEST_UNSUPPORTED_FIELD)
using int2 = int __attribute__((ext_vector_type(2)));
using int16 = int __attribute__((ext_vector_type(16)));
struct WithVector {
  int16 Value;
};

int16 read(WithVector Value) { return Value.Value; }
// UNSUPPORTED-FIELD: error: MMIX GNU ABI does not support vector value CodeGen involving type 'WithVector'
#endif
