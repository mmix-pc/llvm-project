// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu17 \
// RUN:   -emit-llvm -o /dev/null -DBITINT128 %s 2>&1 | FileCheck %s \
// RUN:   --check-prefix=BITINT128-ERR
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu17 \
// RUN:   -emit-llvm -o /dev/null -DCOMPLEX %s 2>&1 | FileCheck %s \
// RUN:   --check-prefix=COMPLEX-ERR
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu17 \
// RUN:   -emit-llvm -o /dev/null -DVECTOR %s 2>&1 | FileCheck %s \
// RUN:   --check-prefix=VECTOR-ERR
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=gnu17 \
// RUN:   -emit-llvm -o /dev/null -DATOMIC %s 2>&1 | FileCheck %s \
// RUN:   --check-prefix=ATOMIC-ERR

typedef int int2 __attribute__((ext_vector_type(2)));
typedef _Atomic(int) atomic_int;

#if defined(BITINT128)
long unsupported(int named, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, named);
  return (long)__builtin_va_arg(ap, _BitInt(128));
}
// BITINT128-ERR: error: signed _BitInt of bit sizes greater than 64 not supported
#elif defined(COMPLEX)
long unsupported(int named, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, named);
  _Complex int value = __builtin_va_arg(ap, _Complex int);
  return 0;
}
// COMPLEX-ERR: error: MMIX GNU ABI does not support va_arg type '_Complex int'
#elif defined(VECTOR)
int2 unsupported(int named, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, named);
  return __builtin_va_arg(ap, int2);
}
// VECTOR-ERR: error: MMIX GNU ABI does not support va_arg type 'int2'
#elif defined(ATOMIC)
void unsupported(int named, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, named);
  (void)__builtin_va_arg(ap, atomic_int);
}
// ATOMIC-ERR: error: second argument to 'va_arg' is of non-POD type 'atomic_int'
#endif
