// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=types -DTYPES %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=types -DTYPES %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=types -DTYPES %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=types -DTYPES %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=vector1 -DVECTOR=1 %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=vector1 -DVECTOR=1 %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=vector1 -DVECTOR=1 %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=vector1 -DVECTOR=1 %s

// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=vector2 -DVECTOR=2 %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=vector2 -DVECTOR=2 %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=vector2 -DVECTOR=2 %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=vector2 -DVECTOR=2 %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=vector3 -DVECTOR=3 %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=vector3 -DVECTOR=3 %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=vector3 -DVECTOR=3 %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=vector3 -DVECTOR=3 %s
// Keep Sema's unsupported formats separate from CodeGen-owned vector checks:
// a Sema error otherwise prevents the vector boundary from being exercised.
#ifdef TYPES
_Float16 half_value; // types-error {{_Float16 is not supported on this target}}
__float128 quad_value; // types-error {{__float128 is not supported on this target}}
_BitInt(128) bitint_value; // types-error {{signed _BitInt of bit sizes greater than 64 not supported}}
#endif
#ifdef VECTOR
typedef __int128 i128x1 __attribute__((vector_size(16)));
typedef unsigned __int128 u128x1 __attribute__((ext_vector_type(1)));
typedef long i64x2 __attribute__((vector_size(16)));
#if VECTOR == 1
i128x1 signed_lane; // vector1-error {{MMIX GNU ABI does not support vector value CodeGen involving type 'i128x1'}}
#endif
#if VECTOR == 2
u128x1 unsigned_lane; // vector2-error {{MMIX GNU ABI does not support vector value CodeGen involving type 'u128x1'}}
#endif
#if VECTOR == 3
i64x2 wide_vector; // vector3-error {{MMIX GNU ABI does not support vector value CodeGen involving type 'i64x2'}}
#endif
#endif
