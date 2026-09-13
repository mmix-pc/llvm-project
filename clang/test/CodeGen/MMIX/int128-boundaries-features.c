// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -fgnuc-version=4.2.1 -mrelocation-model static -emit-obj -o %t.o -verify %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -fgnuc-version=4.2.1 -mrelocation-model static -emit-obj -o %t.o -verify %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -fgnuc-version=4.2.1 -mrelocation-model static -emit-obj -o %t.o -verify %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -fgnuc-version=4.2.1 -mrelocation-model static -emit-obj -o %t.o -verify %s

// expected-no-diagnostics
#if !defined(__SIZEOF_INT128__) || __SIZEOF_INT128__ != 16
#error missing int128 source support
#endif
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16) || \
    defined(__CLANG_ATOMIC_INT128_LOCK_FREE) || defined(__GCC_ATOMIC_INT128_LOCK_FREE)
#error int128 must not advertise lock-free atomics
#endif
#ifdef __FLOAT128__
#error int128 must not enable float128
#endif
#ifdef __cplusplus
#define ASSERT static_assert
#define ALIGNOF alignof
#else
#define ASSERT _Static_assert
#define ALIGNOF _Alignof
#endif
ASSERT(sizeof(__int128) == 16 && ALIGNOF(__int128) == 8, "int128 layout");
ASSERT(sizeof(unsigned __int128) == 16, "unsigned int128 layout");
ASSERT(!__atomic_always_lock_free(sizeof(__int128), 0), "no wide lock freedom");
ASSERT(__atomic_always_lock_free(8, 0), "retain native lock freedom");
ASSERT(__CLANG_ATOMIC_LLONG_LOCK_FREE == 2, "retain Clang atomic macro");
ASSERT(__GCC_ATOMIC_LLONG_LOCK_FREE == 2, "retain GCC atomic macro");
ASSERT(sizeof(long double) == 8 && __LDBL_MANT_DIG__ == 53, "retain long double");
ASSERT(sizeof(long) == 8 && sizeof(void *) == 8, "retain LP64");
ASSERT(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__, "retain endianness");

// Scalar admission must not depend on hosted headers or an OS provider.
__int128 identity(__int128 value) { return value; }
