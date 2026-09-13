// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c11 -fsyntax-only \
// RUN:   -verify %s

#define SAME_TYPE(T, U) __builtin_types_compatible_p(T, U)

_Static_assert(sizeof(_Bool) == 1 && _Alignof(_Bool) == 1, "_Bool");
_Static_assert(sizeof(char) == 1 && _Alignof(char) == 1, "char");
_Static_assert((char)-1 < 0, "plain char is signed");
_Static_assert(sizeof(short) == 2 && _Alignof(short) == 2, "short");
_Static_assert(sizeof(int) == 4 && _Alignof(int) == 4, "int");
_Static_assert(sizeof(long) == 8 && _Alignof(long) == 8, "long");
_Static_assert(sizeof(long long) == 8 && _Alignof(long long) == 8,
               "long long");
_Static_assert(sizeof(__int128) == 16 && _Alignof(__int128) == 8, "__int128");

_Static_assert(sizeof(float) == 4 && _Alignof(float) == 4, "float");
_Static_assert(sizeof(double) == 8 && _Alignof(double) == 8, "double");
_Static_assert(sizeof(long double) == 8 && _Alignof(long double) == 8,
               "long double");
_Static_assert(!SAME_TYPE(double, long double), "distinct floating types");

_Static_assert(sizeof(void *) == 8 && _Alignof(void *) == 8, "object pointer");
_Static_assert(sizeof(void (*)(void)) == 8 &&
                   _Alignof(void (*)(void)) == 8,
               "function pointer");

_Static_assert(SAME_TYPE(__SIZE_TYPE__, unsigned long), "size_t");
_Static_assert(SAME_TYPE(__PTRDIFF_TYPE__, long), "ptrdiff_t");
_Static_assert(SAME_TYPE(__INTPTR_TYPE__, long), "intptr_t");
_Static_assert(SAME_TYPE(__UINTPTR_TYPE__, unsigned long), "uintptr_t");
_Static_assert(SAME_TYPE(__INTMAX_TYPE__, long), "intmax_t");
_Static_assert(SAME_TYPE(__UINTMAX_TYPE__, unsigned long), "uintmax_t");
_Static_assert(SAME_TYPE(__WCHAR_TYPE__, int), "wchar_t");
_Static_assert(SAME_TYPE(__WINT_TYPE__, unsigned int), "wint_t");

enum SmallEnum { SmallZero, SmallOne };
enum SignedEnum { SignedMinusOne = -1, SignedZero };
_Static_assert(sizeof(enum SmallEnum) == 4 && _Alignof(enum SmallEnum) == 4,
               "default enum");
_Static_assert(sizeof(enum SignedEnum) == 4 && _Alignof(enum SignedEnum) == 4,
               "signed enum");

_Float16 unsupported_half; // expected-error {{_Float16 is not supported on this target}}
__float128 unsupported_quad; // expected-error {{__float128 is not supported on this target}}
_BitInt(17) parsed_bitint;
_BitInt(65) oversized_bitint; // expected-error {{signed _BitInt of bit sizes greater than 64 not supported}}
