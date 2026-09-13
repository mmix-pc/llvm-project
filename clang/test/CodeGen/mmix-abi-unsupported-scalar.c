// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -emit-llvm -o /dev/null \
// RUN:   -DTEST_BITINT128 %s 2>&1 | FileCheck %s --check-prefix=BITINT128
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -emit-llvm -o /dev/null \
// RUN:   -DTEST_COMPLEX %s 2>&1 | FileCheck %s --check-prefix=COMPLEX
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -emit-llvm -o /dev/null \
// RUN:   -DTEST_VARIADIC_COMPLEX %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=VARIADIC-COMPLEX
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -emit-llvm -o /dev/null \
// RUN:   -DTEST_VECTOR %s 2>&1 | FileCheck %s --check-prefix=VECTOR
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -emit-llvm -o /dev/null \
// RUN:   -DTEST_ADDRESS_SPACE %s 2>&1 | FileCheck %s --check-prefix=AS
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -emit-llvm -o /dev/null \
// RUN:   -DTEST_INDIRECT_CALL %s 2>&1 | FileCheck %s --check-prefix=CALL

#if defined(TEST_BITINT128)
_BitInt(128) unsupported(_BitInt(128) value) { return value; }
// BITINT128-COUNT-2: error: signed _BitInt of bit sizes greater than 64 not supported
#elif defined(TEST_COMPLEX)
_Complex int unsupported(_Complex int value) { return value; }
// COMPLEX: error: MMIX GNU ABI does not support return type '_Complex int'
// COMPLEX: error: MMIX GNU ABI does not support argument type '_Complex int'
#elif defined(TEST_VARIADIC_COMPLEX)
extern void variadic_sink(int, ...);
void unsupported(_Complex int value) { variadic_sink(0, value); }
// VARIADIC-COMPLEX: error: MMIX GNU ABI does not support argument type '_Complex int'
#elif defined(TEST_VECTOR)
typedef int int4 __attribute__((ext_vector_type(4)));
int4 unsupported(int4 value) { return value; }
// VECTOR: error: MMIX GNU ABI does not support return type 'int4'
// VECTOR: error: MMIX GNU ABI does not support argument type 'int4'
#elif defined(TEST_ADDRESS_SPACE)
typedef int __attribute__((address_space(1))) as1_int;
as1_int *unsupported(as1_int *value) { return value; }
// AS: error: MMIX GNU ABI does not support return type 'as1_int *'
// AS: error: MMIX GNU ABI does not support argument type 'as1_int *'
#elif defined(TEST_INDIRECT_CALL)
typedef _BitInt(128) (*unsupported_function)(_BitInt(128));
void call_unsupported(unsupported_function fn) { (void)fn(0); }
// CALL-COUNT-2: error: signed _BitInt of bit sizes greater than 64 not supported
#endif
