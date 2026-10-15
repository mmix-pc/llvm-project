// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model pic -std=c++17 -fexceptions -fcxx-exceptions -exception-model=dwarf -emit-llvm -o - %s | FileCheck %s

struct Base { virtual ~Base() = default; };
struct Other { virtual ~Other() = default; };
struct Derived : Base, Other {};
struct __attribute__((visibility("hidden"))) Private : Base {};
namespace { struct Local : Base {}; }
namespace std { class type_info; }
const std::type_info &public_type() { return typeid(Derived); }
const std::type_info &private_type() { return typeid(Private); }
const std::type_info &local_type() { return typeid(Local); }
Other *cross_cast(Base *p) { return dynamic_cast<Other *>(p); }
void raise() { throw Derived(); }
int receive(void (*callback)()) {
  try { callback(); }
  catch (const Base &) { throw; }
  return 0;
}

// Public descriptors remain interposable and COMDAT-owned, not dso_local.
// CHECK-DAG: @_ZTI4Base = linkonce_odr constant
// CHECK-DAG: @_ZTI5Other = linkonce_odr constant
// CHECK-DAG: @_ZTI7Derived = linkonce_odr constant {{.*}}ptr @_ZTI4Base, i64 2, ptr @_ZTI5Other, i64 2050 }, comdat, align 8
// CHECK-DAG: @_ZTI7Private = linkonce_odr hidden constant
// CHECK-DAG: @_ZTIN12_GLOBAL__N_15LocalE = internal constant
// CHECK: call ptr @__dynamic_cast({{.*}}ptr @_ZTI4Base, ptr @_ZTI5Other, i64 -2)
// CHECK: call void @__cxa_throw({{.*}}ptr @_ZTI7Derived
// CHECK: personality ptr @__gxx_personality_v0
// CHECK: catch ptr @_ZTI4Base
// CHECK: invoke void @__cxa_rethrow()
