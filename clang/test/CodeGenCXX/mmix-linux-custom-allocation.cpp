// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -fcxx-exceptions -fexceptions -exception-model=dwarf -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -O2 -emit-obj -o %t.o %s
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -mrelocation-model static -std=c++17 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=GENERIC
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -DNEGATIVE=1 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=NEGATIVE
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -DNEGATIVE=2 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=NEGATIVE
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -DNEGATIVE=3 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=NEGATIVE

using size_t = __SIZE_TYPE__;
struct Allocator {};
void *operator new(size_t, const Allocator &);
void operator delete(void *, const Allocator &) noexcept;
struct Buffer {
  Buffer();
  ~Buffer();
  static void operator delete(void *) noexcept;
  static void operator delete(void *, const Allocator &) noexcept;
};
extern "C" Buffer *named(const Allocator &allocator) {
  return new (allocator) Buffer;
}
// CHECK-LABEL: define{{.*}} @named(
// CHECK: call{{.*}} @_ZnwmRK9Allocator
// CHECK: invoke{{.*}} @_ZN6BufferC1Ev
// CHECK: call{{.*}} @_ZN6BufferdlEPvRK9Allocator
extern "C" void release(Buffer *buffer) { delete buffer; }
// CHECK-LABEL: define{{.*}} @release(
// CHECK: @_ZN6BufferD1Ev
// CHECK: @_ZN6BufferdlEPv

struct Node {
  Node();
  static void *operator new(size_t, Allocator &, size_t = 16) noexcept;
  static void operator delete(void *, Allocator &, size_t) noexcept;
};
extern "C" Node *arena(Allocator &allocator) { return new (allocator) Node; }
// CHECK-LABEL: define{{.*}} @arena(
// CHECK: call{{.*}} @_ZN4NodenwEmR9Allocatorm({{.*}}i64{{.*}}16)
// CHECK: invoke{{.*}} @_ZN4NodeC1Ev
// CHECK: call{{.*}} @_ZN4NodedlEPvR9Allocatorm
// GENERIC: error: MMIX does not support C++ allocation form

#if NEGATIVE == 1
struct ByValue { int tag; };
void *operator new(size_t, ByValue);
int *aggregate_argument(ByValue value) { return new (value) int; }
// NEGATIVE: error: MMIX does not support C++ allocation form
#elif NEGATIVE == 2
struct Array {
  static void *operator new[](size_t, Allocator &);
};
Array *custom_array(Allocator &allocator) { return new (allocator) Array[2]; }
#elif NEGATIVE == 3
struct Variadic {
  static void *operator new(size_t, ...);
};
Variadic *variadic_argument() { return new (1) Variadic; }
#endif
