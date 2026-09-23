// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -fsized-deallocation -fcxx-exceptions -fexceptions -exception-model=dwarf -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -fsized-deallocation -fcxx-exceptions -fexceptions -exception-model=dwarf -O0 -emit-obj -o %t.o %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -fsized-deallocation -fcxx-exceptions -fexceptions -exception-model=dwarf -O2 -emit-obj -o %t.o %s

using size_t = __SIZE_TYPE__;
namespace std { enum class align_val_t : size_t {}; }
struct Empty {};
struct One { unsigned count; };
struct Two { unsigned count, bytes; };

struct Plain {
  Plain();
  ~Plain();
  static void *operator new(size_t);
  static void operator delete(void *, size_t) noexcept;
};
extern "C" Plain *plain() { return new Plain; }
// CHECK-LABEL: define{{.*}} @plain(
// CHECK: call{{.*}} @_ZN5PlainnwEm
// CHECK: invoke{{.*}} @_ZN5PlainC1Ev
// CHECK: call void @_ZN5PlaindlEPvm
extern "C" void release(Plain *p) { delete p; }
// CHECK-LABEL: define{{.*}} @release(
// CHECK: @_ZN5PlainD1Ev
// CHECK: @_ZN5PlaindlEPvm

struct Slots {
  long value;
  Slots();
  static void *operator new(size_t, Empty);
  static void *operator new(size_t, One);
  static void *operator new(size_t, Two);
  static void operator delete(void *, Empty) noexcept;
  static void operator delete(void *, One) noexcept;
  static void operator delete(void *, Two) noexcept;
};
extern "C" Slots *empty() { return new (Empty{}) Slots; }
// CHECK-LABEL: define{{.*}} @empty(
// CHECK: call{{.*}} @_ZN5SlotsnwEm5Empty(i64 noundef 8)
// CHECK: invoke{{.*}} @_ZN5SlotsC1Ev
// CHECK: call void @_ZN5SlotsdlEPv5Empty
extern "C" Slots *one(unsigned n) { return new (One{n}) Slots; }
// CHECK-LABEL: define{{.*}} @one(
// CHECK: call{{.*}} @_ZN5SlotsnwEm3One
// CHECK: invoke{{.*}} @_ZN5SlotsC1Ev
// CHECK: call void @_ZN5SlotsdlEPv3One
extern "C" Slots *two(unsigned n, unsigned b) { return new (Two{n, b}) Slots; }
// CHECK-LABEL: define{{.*}} @two(
// CHECK: call{{.*}} @_ZN5SlotsnwEm3Two
// CHECK: invoke{{.*}} @_ZN5SlotsC1Ev
// CHECK: call void @_ZN5SlotsdlEPv3Two

struct Array {
  long value;
  Array();
  ~Array();
  static void *operator new[](size_t, Two) noexcept;
  static void operator delete[](void *, Two) noexcept;
  static void operator delete[](void *, size_t) noexcept;
};
extern "C" Array *array(size_t n) { return new (Two{3, 4}) Array[n]; }
// CHECK-LABEL: define{{.*}} @array(
// CHECK: call{{.*}} @_ZN5ArraynaEm3Two
// CHECK: icmp eq ptr {{.*}}, null
// CHECK: store i64
// CHECK: invoke{{.*}} @_ZN5ArrayC1Ev
// CHECK: @_ZN5ArrayD1Ev
// CHECK: @_ZN5ArraydaEPv3Two
extern "C" void release_array(Array *p) { delete[] p; }
// CHECK-LABEL: define{{.*}} @release_array(
// CHECK: load i64
// CHECK: @_ZN5ArrayD1Ev
// CHECK: @_ZN5ArraydaEPvm

struct alignas(32) Aligned {
  Aligned();
  ~Aligned();
  static void *operator new(size_t, std::align_val_t, Two);
  static void operator delete(void *, std::align_val_t, Two) noexcept;
  static void operator delete(void *, size_t, std::align_val_t) noexcept;
};
extern "C" Aligned *aligned() { return new (Two{1, 2}) Aligned; }
// CHECK-LABEL: define{{.*}} @aligned(
// CHECK: @_ZN7AlignednwEmSt11align_val_t3Two(i64 noundef 32, i64 noundef 32,
// CHECK: invoke{{.*}} @_ZN7AlignedC1Ev
// CHECK: @_ZN7AligneddlEPvSt11align_val_t3Two
extern "C" void release_aligned(Aligned *p) { delete p; }
// CHECK-LABEL: define{{.*}} @release_aligned(
// CHECK: @_ZN7AlignedD1Ev
// CHECK: @_ZN7AligneddlEPvmSt11align_val_t

struct Argument { Argument(); Argument(const Argument &); ~Argument(); };
void *operator new(size_t, Argument);
void operator delete(void *, Argument) noexcept;
extern "C" Plain *nontrivial(Argument &a) { return ::new (a) Plain; }
// CHECK-LABEL: define{{.*}} @nontrivial(
// CHECK: @_ZN8ArgumentC1ERKS_
// CHECK: @_Znwm8Argument
// CHECK: invoke{{.*}} @_ZN5PlainC1Ev
// CHECK: @_ZdlPv8Argument
// CHECK: @_ZN8ArgumentD1Ev
