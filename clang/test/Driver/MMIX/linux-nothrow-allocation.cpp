// REQUIRES: mmix-registered-target
// RUN: %clangxx --target=mmix-unknown-linux -std=c++17 -O0 -S -emit-llvm %s -o - | FileCheck %s
// RUN: %clangxx --target=mmix-unknown-linux -std=c++17 -O0 -c %s -o %t.o
// RUN: %clangxx --target=mmix-unknown-linux -std=c++17 -O2 -c %s -o %t.opt.o
// RUN: not %clangxx --target=mmix-unknown-unknown -std=c++17 -c %s -o %t.generic.o 2>&1 | FileCheck %s --check-prefix=GENERIC
// GENERIC: MMIX does not support C++ allocation form

using size_t = decltype(sizeof(0));
namespace std {
struct nothrow_t {};
extern const nothrow_t nothrow;
enum class align_val_t : size_t {};
}
void *operator new(size_t, std::align_val_t, const std::nothrow_t &) noexcept;
void *operator new[](size_t, std::align_val_t, const std::nothrow_t &) noexcept;
void *operator new[](size_t, const std::nothrow_t &) noexcept;
void operator delete(void *, std::align_val_t, const std::nothrow_t &) noexcept;
void operator delete[](void *, std::align_val_t, const std::nothrow_t &) noexcept;
void operator delete[](void *, const std::nothrow_t &) noexcept;

struct alignas(64) Aligned { Aligned(); ~Aligned(); long value; };
struct Plain { Plain(); ~Plain(); long value; };
Aligned *single() { return new (std::nothrow) Aligned; }
// CHECK: call {{.*}} @_ZnwmSt11align_val_tRKSt9nothrow_t
// CHECK: icmp eq ptr
// CHECK: invoke void @_ZN7AlignedC1Ev
// CHECK: landingpad
// CHECK: call void @_ZdlPvSt11align_val_tRKSt9nothrow_t
Aligned *array(size_t n) { return new (std::nothrow) Aligned[n]; }
// CHECK: call {{.*}} @_ZnamSt11align_val_tRKSt9nothrow_t
// CHECK: icmp eq ptr
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 56
// CHECK: store i64
// CHECK: invoke void @_ZN7AlignedC1Ev
// CHECK: landingpad
// CHECK: @_ZN7AlignedD1Ev
// CHECK: call void @_ZdaPvSt11align_val_tRKSt9nothrow_t
Plain *plain_array(size_t n) { return new (std::nothrow) Plain[n]; }
// CHECK: call {{.*}} @_ZnamRKSt9nothrow_t
// CHECK: invoke void @_ZN5PlainC1Ev
// CHECK: landingpad
// CHECK: @_ZN5PlainD1Ev
// CHECK: call void @_ZdaPvRKSt9nothrow_t
