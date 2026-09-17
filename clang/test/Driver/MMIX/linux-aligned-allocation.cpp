// REQUIRES: mmix-registered-target
// RUN: %clangxx --target=mmix-unknown-linux -std=c++17 -ffreestanding -O0 -S -emit-llvm %s -o - | FileCheck %s
// RUN: %clangxx --target=mmix-unknown-linux -std=c++17 -ffreestanding -O0 -c %s -o %t.o
// RUN: %clangxx --target=mmix-unknown-linux -std=c++17 -ffreestanding -O2 -c %s -o %t.opt.o
// RUN: not %clangxx --target=mmix-unknown-unknown -std=c++17 -ffreestanding -c %s -o %t.generic.o 2>&1 | FileCheck %s --check-prefix=GENERIC
// RUN: not %clangxx --target=mmix-unknown-unknown -std=c++17 -ffreestanding -DDELETE_ONLY -c %s -o %t.generic.o 2>&1 | FileCheck %s --check-prefix=GENERIC-DELETE
// GENERIC: MMIX does not support C++ allocation form
// GENERIC-DELETE: MMIX does not support C++ deallocation form

struct alignas(64) Plain { long x; };
struct alignas(64) Object { Object(); ~Object(); long x; };

#ifndef DELETE_ONLY
Plain *scalar() { return new Plain; }
// CHECK: call {{.*}} @_ZnwmSt11align_val_t(i64{{.*}} 64, i64{{.*}} 64)
#endif
void release(Plain *p) { delete p; }
// CHECK: call void @_ZdlPvmSt11align_val_t(ptr{{.*}}, i64{{.*}} 64, i64{{.*}} 64)
#ifndef DELETE_ONLY
Plain *array(unsigned long n) { return new Plain[n]; }
// CHECK: call {{.*}} @_ZnamSt11align_val_t(i64{{.*}}, i64{{.*}} 64)
void release_array(Plain *p) { delete[] p; }
// CHECK: call void @_ZdaPvSt11align_val_t(ptr{{.*}}, i64{{.*}} 64)
Object *objects(unsigned long n) { return new Object[n]; }
// CHECK: @_ZnamSt11align_val_t
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 56
// CHECK: store i64
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 64
// CHECK: invoke void @_ZN6ObjectC1Ev
// CHECK: landingpad
// CHECK: @_ZdaPvSt11align_val_t
void destroy(Object *p) { delete[] p; }
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 -64
// CHECK: @_ZN6ObjectD1Ev
// CHECK: @_ZdaPvmSt11align_val_t
#endif
