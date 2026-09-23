// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix -mrelocation-model static -std=c++17 -emit-llvm -o %t.ll %s
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: %clang_cc1 -triple mmix -mrelocation-model static -std=c++17 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mmix -mrelocation-model static -std=c++17 -emit-obj -o %t.o %s
// RUN: llvm-readobj --relocations %t.o | FileCheck %s --check-prefix=OBJ
// RUN: not %clang_cc1 -triple mmix -mrelocation-model static -std=c++17 -fno-rtti -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=OFF

namespace std {
class type_info {
public:
  const char *name() const;
  bool operator==(const type_info &) const;
};
}
struct Base { virtual void f(); };
struct Other { virtual void g(); };
struct Derived : Base, Other {};
struct Virtual : virtual Base {};
extern Base *evaluate();

extern "C" const std::type_info *static_type() { return &typeid(const int); }
extern "C" const std::type_info *dynamic_type(Base *p) { return &typeid(*p); }
extern "C" const std::type_info *evaluated_type() { return &typeid(*evaluate()); }
extern "C" const std::type_info *unevaluated_type() { return &typeid(evaluate()); }
extern "C" Derived *down(Base *p) { return dynamic_cast<Derived *>(p); }
extern "C" Derived *secondary(Other *p) { return dynamic_cast<Derived *>(p); }
extern "C" Other *cross(Base *p) { return dynamic_cast<Other *>(p); }
extern "C" Virtual *virtual_down(Base *p) { return dynamic_cast<Virtual *>(p); }
extern "C" Derived *reference(Base &p) { return &dynamic_cast<Derived &>(p); }
extern "C" void *complete(Base *p) { return dynamic_cast<void *>(p); }
extern "C" Base *up(Derived *p) { return dynamic_cast<Base *>(p); }

extern "C" const char *type_name(Base &p) { return typeid(p).name(); }
extern "C" bool same_type(Base &a, Base &b) { return typeid(a) == typeid(b); }
extern void observe(const std::type_info &);
struct Lifetime : Base {
  Lifetime();
  ~Lifetime();
};
Lifetime::Lifetime() { observe(typeid(*this)); }
Lifetime::~Lifetime() { observe(typeid(*this)); }

// IR-LABEL: define{{.*}} @static_type(
// IR: ret ptr @_ZTIi
// IR-LABEL: define{{.*}} @dynamic_type(
// IR: icmp eq ptr
// IR: call void @__cxa_bad_typeid()
// IR: unreachable
// IR: getelementptr inbounds {{(ptr, ptr .*i64 -1|i8, ptr .*i64 -8)}}
// IR-LABEL: define{{.*}} @evaluated_type(
// IR: call{{.*}} @_Z8evaluatev()
// IR-LABEL: define{{.*}} @unevaluated_type(
// IR-NOT: call
// IR: ret ptr @_ZTIP4Base
// IR-LABEL: define{{.*}} @down(
// IR: call ptr @__dynamic_cast(ptr {{.*}}, ptr {{(nonnull )?}}@_ZTI4Base, ptr {{(nonnull )?}}@_ZTI7Derived, i64 0)
// IR-LABEL: define{{.*}} @secondary(
// IR: call ptr @__dynamic_cast(ptr {{.*}}, ptr {{(nonnull )?}}@_ZTI5Other, ptr {{(nonnull )?}}@_ZTI7Derived, i64 8)
// IR-LABEL: define{{.*}} @cross(
// IR: call ptr @__dynamic_cast(ptr {{.*}}, ptr {{(nonnull )?}}@_ZTI4Base, ptr {{(nonnull )?}}@_ZTI5Other, i64 -2)
// IR-LABEL: define{{.*}} @virtual_down(
// IR: call ptr @__dynamic_cast(ptr {{.*}}, ptr {{(nonnull )?}}@_ZTI4Base, ptr {{(nonnull )?}}@_ZTI7Virtual, i64 -1)
// IR-LABEL: define{{.*}} @reference(
// IR: call ptr @__dynamic_cast
// IR: call void @__cxa_bad_cast()
// IR: unreachable
// IR-LABEL: define{{.*}} @complete(
// IR-NOT: @__dynamic_cast
// IR: getelementptr inbounds {{(i64, ptr .*i64 -2|i8, ptr .*i64 -16)}}
// IR-LABEL: define{{.*}} @up(
// IR-NOT: @__dynamic_cast
// IR: ret ptr
// IR-LABEL: define{{.*}} @type_name(
// IR: call{{.*}} @_ZNKSt9type_info4nameEv(
// IR-LABEL: define{{.*}} @same_type(
// IR: call{{.*}} @_ZNKSt9type_infoeqERKS_(
// IR-LABEL: define{{.*}} @_ZN8LifetimeC2Ev(
// IR: store ptr getelementptr inbounds {{.*}}@_ZTV8Lifetime
// IR: call{{.*}} @_Z7observeRKSt9type_info(
// IR-LABEL: define{{.*}} @_ZN8LifetimeD2Ev(
// IR: store ptr getelementptr inbounds {{.*}}@_ZTV8Lifetime
// IR: call{{.*}} @_Z7observeRKSt9type_info(
// OBJ-DAG: R_MMIX_GETA __cxa_bad_typeid
// OBJ-DAG: R_MMIX_GETA __cxa_bad_cast
// OBJ-DAG: R_MMIX_GETA __dynamic_cast
// OFF-DAG: error: use of typeid requires -frtti
// OFF-DAG: error: use of dynamic_cast requires -frtti
