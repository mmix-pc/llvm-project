// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -ftls-model=local-exec -std=c++17 -emit-llvm -o /dev/null -verify -DGLOBAL %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -ftls-model=local-exec -std=c++17 -emit-llvm -o /dev/null -verify -DARRAY %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -ftls-model=local-exec -std=c++17 -emit-llvm -o /dev/null -verify -DTEMPORARY %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -ftls-model=local-exec -std=c++17 -emit-llvm -o /dev/null -verify %s
struct Object { ~Object(); };
#if defined(GLOBAL)
thread_local Object global; // expected-error {{MMIX Linux does not yet support C++ TLS thread-exit cleanup}}
#elif defined(ARRAY)
thread_local Object array[2]; // expected-error {{MMIX Linux does not yet support C++ TLS thread-exit cleanup}}
#elif defined(TEMPORARY)
thread_local const Object &temporary = Object(); // expected-error {{MMIX Linux does not yet support C++ TLS thread-exit cleanup}}
#else
Object &local() {
  static thread_local Object value; // expected-error {{MMIX Linux does not yet support C++ TLS thread-exit cleanup}}
  return value;
}
#endif
