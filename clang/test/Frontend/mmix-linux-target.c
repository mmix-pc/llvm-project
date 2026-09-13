// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -std=c17 -fsyntax-only %s
// RUN: %clang_cc1 -triple mmix-unknown-linux-unknown -std=c17 -fsyntax-only %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=c++17 -fsyntax-only %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -DBARE -std=c17 -fsyntax-only %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -DBARE -x c++ -std=c++17 -fsyntax-only %s
// RUN: not %clang_cc1 -triple mmix-unknown-linux -DTEST_TLS -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=TLS
// RUN: not %clang_cc1 -triple mmix-pc-linux -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=VENDOR
// RUN: not %clang_cc1 -triple mmix-unknown-linux-gnu -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=GNU
// RUN: not %clang_cc1 -triple mmix-unknown-linux-musl -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=ENV
// RUN: not %clang_cc1 -triple mmix-unknown-linux-android -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=ANDROID
// RUN: not %clang_cc1 -triple mmix-unknown-linux-elf -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=ELF
// RUN: not %clang_cc1 -triple mmix-unknown-linux-none -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=NONE
// RUN: not %clang_cc1 -triple mmix-unknown-linux6 -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=VERSION
// VENDOR: error: unknown target triple 'mmix-pc-linux'
// GNU: error: unknown target triple 'mmix-unknown-linux-gnu'
// ENV: error: unknown target triple 'mmix-unknown-linux-musl'
// ANDROID: error: unknown target triple 'mmix-unknown-linux-android'
// ELF: error: unknown target triple 'mmix-unknown-linux-elf'
// NONE: error: unknown target triple 'mmix-unknown-linux-none'
// VERSION: error: unknown target triple 'mmix-unknown-linux6'
// TLS: error: thread-local storage is not supported for the current target

#if defined(BARE)
#if defined(__linux__) || defined(__unix__) || defined(__gnu_linux__) || defined(_GNU_SOURCE)
#error Linux macros leaked to bare metal
#endif
#else
#if !defined(__linux__) || !defined(__unix__) || !defined(__gnu_linux__)
#error Missing Linux OS macros
#endif
#if defined(__cplusplus) && !defined(_GNU_SOURCE)
#error Missing Linux C++ extension macro
#endif
#endif

#if !defined(__mmix__) || !defined(__MMIX_ABI_GNU__) || !defined(__ELF__)
#error Missing MMIX identity
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ || __STDC_NO_THREADS__ != 1
#error Unexpected architectural capability
#endif
#if __SIZEOF_INT128__ != 16 || defined(__FLOAT128__) || defined(__ANDROID__)
#error Unexpected type or OS capability
#endif

#ifdef __cplusplus
#define ASSERT static_assert
#define ALIGNOF alignof
#else
#define ASSERT _Static_assert
#define ALIGNOF _Alignof
#endif
ASSERT(sizeof(void *) == 8 && ALIGNOF(void *) == 8, "pointers");
ASSERT(sizeof(long) == 8 && sizeof(int) == 4, "LP64");
ASSERT(sizeof(long double) == 8 && ALIGNOF(long double) == 8, "long double");
ASSERT(__WCHAR_WIDTH__ == 32 && __WINT_WIDTH__ == 32, "wide characters");
ASSERT((__WCHAR_TYPE__)-1 < 0 && (__WINT_TYPE__)-1 > 0, "wide character signedness");
ASSERT(__atomic_always_lock_free(8, 0), "atomic capability");
struct Record { char c; long n; };
ASSERT(sizeof(struct Record) == 16 && __builtin_offsetof(struct Record, n) == 8, "records");

#ifdef TEST_TLS
_Thread_local int tls;
#endif
