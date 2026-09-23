// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-llvm -o %t/cxa.ll %t/source.cpp
// RUN: FileCheck %s --check-prefixes=CXA-IR,CXA-O0 < %t/cxa.ll
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-llvm -o %t/cxa-opt.ll %t/source.cpp
// RUN: FileCheck %s --check-prefixes=CXA-IR,CXA-O2 < %t/cxa-opt.ll
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -fno-use-cxa-atexit -mrelocation-model static -emit-llvm \
// RUN:   -o %t/atexit.ll %t/source.cpp
// RUN: FileCheck %s --check-prefix=ATEXIT-O0 \
// RUN:   --implicit-check-not=__dso_handle < %t/atexit.ll
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -fno-use-cxa-atexit -mrelocation-model static -emit-llvm \
// RUN:   -o %t/atexit-opt.ll %t/source.cpp
// RUN: FileCheck %s --check-prefix=ATEXIT-O2 \
// RUN:   --implicit-check-not=__dso_handle < %t/atexit-opt.ll
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/cxa.o %t/source.cpp
// RUN: llvm-nm --undefined-only %t/cxa.o \
// RUN:   | FileCheck %s --check-prefix=CXA-UNDEFINED
// RUN: llvm-readobj --sections --symbols --relocations %t/cxa.o \
// RUN:   | FileCheck %s --check-prefix=CXA-OBJECT
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/cxa-opt.o %t/source.cpp
// RUN: llvm-nm --undefined-only %t/cxa-opt.o \
// RUN:   | FileCheck %s --check-prefix=CXA-UNDEFINED
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -fno-use-cxa-atexit -mrelocation-model static -emit-obj \
// RUN:   -o %t/atexit.o %t/source.cpp
// RUN: llvm-nm --undefined-only %t/atexit.o \
// RUN:   | FileCheck %s --check-prefix=ATEXIT-UNDEFINED
// RUN: llvm-readobj --sections --symbols --relocations %t/atexit.o \
// RUN:   | FileCheck %s --check-prefix=ATEXIT-OBJECT
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -fno-use-cxa-atexit -mrelocation-model static -emit-obj \
// RUN:   -o %t/atexit-opt.o %t/source.cpp
// RUN: llvm-nm --undefined-only %t/atexit-opt.o \
// RUN:   | FileCheck %s --check-prefix=ATEXIT-UNDEFINED
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/runtime.o %t/runtime.cpp
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/cxa-linked %t/cxa.o %t/runtime.o
// RUN: llvm-nm --undefined-only %t/cxa-linked | count 0
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/cxa-opt-linked %t/cxa-opt.o \
// RUN:   %t/runtime.o
// RUN: llvm-nm --undefined-only %t/cxa-opt-linked | count 0
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/atexit-linked %t/atexit.o \
// RUN:   %t/runtime.o
// RUN: llvm-nm --undefined-only %t/atexit-linked | count 0
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/atexit-opt-linked \
// RUN:   %t/atexit-opt.o %t/runtime.o
// RUN: llvm-nm --undefined-only %t/atexit-opt-linked | count 0

//--- source.cpp
extern long make_value(long);
extern void observe(long);

struct Value {
  long value;
  explicit Value(long input) : value(input) {}
  ~Value();
};

Value::~Value() { observe(value); }

Value global(make_value(1));

long local() {
  static Value value(make_value(2));
  return value.value;
}

//--- runtime.cpp
extern "C" {
char __dso_handle = 0;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
int __cxa_guard_acquire(void *) { return 1; }
void __cxa_guard_release(void *) {}
int atexit(void (*)()) { return 0; }
long c_entry() { return 0; }
}

long make_value(long value) { return value; }
void observe(long) {}

// CXA-IR: @__dso_handle = external hidden global i8
// CXA-IR: @llvm.global_ctors = appending global
// CXA-O0-LABEL: define {{.*}}void @_ZN5ValueD2Ev(
// CXA-O0: call void @_Z7observel(i64 noundef
// CXA-O0-LABEL: define internal void @__cxx_global_var_init()
// CXA-O0: call noundef i64 @_Z10make_valuel(i64 noundef 1)
// CXA-O0: call i32 @__cxa_atexit(ptr @_ZN5ValueD1Ev, ptr @global, ptr @__dso_handle)
// CXA-O0-LABEL: define {{.*}}i64 @_Z5localv()
// CXA-O0: call i32 @__cxa_guard_acquire(ptr {{.*}}@_ZGVZ5localvE5value)
// CXA-O0: call noundef i64 @_Z10make_valuel(i64 noundef 2)
// CXA-O0: call i32 @__cxa_atexit(ptr @_ZN5ValueD1Ev, ptr {{.*}}@_ZZ5localvE5value, ptr @__dso_handle)
// CXA-O0: call void @__cxa_guard_release(ptr {{.*}}@_ZGVZ5localvE5value)
// CXA-O2-LABEL: define {{.*}}void @_ZN5ValueD2Ev(
// CXA-O2: call void @_Z7observel(i64 noundef
// CXA-O2-LABEL: define {{.*}}i64 @_Z5localv()
// CXA-O2: call i32 @__cxa_guard_acquire(ptr {{.*}}@_ZGVZ5localvE5value)
// CXA-O2: call noundef i64 @_Z10make_valuel(i64 noundef 2)
// CXA-O2: call i32 @__cxa_atexit(ptr {{.*}}@_ZN5ValueD1Ev, ptr {{.*}}@_ZZ5localvE5value{{.*}}, ptr {{.*}}@__dso_handle)
// CXA-O2: call void @__cxa_guard_release(ptr {{.*}}@_ZGVZ5localvE5value)
// CXA-O2-LABEL: define internal void @_GLOBAL__sub_I_source.cpp()
// CXA-O2: call noundef i64 @_Z10make_valuel(i64 noundef 1)
// CXA-O2: call i32 @__cxa_atexit(ptr {{.*}}@_ZN5ValueD1Ev, ptr {{.*}}@global, ptr {{.*}}@__dso_handle)

// ATEXIT-O0-LABEL: define internal void @__cxx_global_var_init()
// ATEXIT-O0: call noundef i64 @_Z10make_valuel(i64 noundef 1)
// ATEXIT-O0: call i32 @atexit(ptr @__dtor_global)
// ATEXIT-O0-LABEL: define internal void @__dtor_global()
// ATEXIT-O0: call void @_ZN5ValueD1Ev(ptr {{.*}}@global)
// ATEXIT-O0-LABEL: define {{.*}}i64 @_Z5localv()
// ATEXIT-O0: call i32 @__cxa_guard_acquire(ptr {{.*}}@_ZGVZ5localvE5value)
// ATEXIT-O0: call noundef i64 @_Z10make_valuel(i64 noundef 2)
// ATEXIT-O0: call i32 @atexit(ptr @__dtor__ZZ5localvE5value)
// ATEXIT-O0: call void @__cxa_guard_release(ptr {{.*}}@_ZGVZ5localvE5value)
// ATEXIT-O0-LABEL: define internal void @__dtor__ZZ5localvE5value()
// ATEXIT-O0: call void @_ZN5ValueD1Ev(ptr {{.*}}@_ZZ5localvE5value)
// ATEXIT-O2-LABEL: define internal void @__dtor_global()
// ATEXIT-O2: call void @_Z7observel(i64 noundef
// ATEXIT-O2-LABEL: define {{.*}}i64 @_Z5localv()
// ATEXIT-O2: call i32 @__cxa_guard_acquire(ptr {{.*}}@_ZGVZ5localvE5value)
// ATEXIT-O2: call noundef i64 @_Z10make_valuel(i64 noundef 2)
// ATEXIT-O2: call i32 @atexit(ptr {{.*}}@__dtor__ZZ5localvE5value)
// ATEXIT-O2: call void @__cxa_guard_release(ptr {{.*}}@_ZGVZ5localvE5value)
// ATEXIT-O2-LABEL: define internal void @__dtor__ZZ5localvE5value()
// ATEXIT-O2: call void @_Z7observel(i64 noundef
// ATEXIT-O2-LABEL: define internal void @_GLOBAL__sub_I_source.cpp()
// ATEXIT-O2: call noundef i64 @_Z10make_valuel(i64 noundef 1)
// ATEXIT-O2: call i32 @atexit(ptr {{.*}}@__dtor_global)

// CXA-UNDEFINED-DAG: U _Z10make_valuel
// CXA-UNDEFINED-DAG: U _Z7observel
// CXA-UNDEFINED-DAG: U __cxa_atexit
// CXA-UNDEFINED-DAG: U __cxa_guard_acquire
// CXA-UNDEFINED-DAG: U __cxa_guard_release
// CXA-UNDEFINED-DAG: U __dso_handle
// CXA-UNDEFINED-NOT: U

// ATEXIT-UNDEFINED-DAG: U _Z10make_valuel
// ATEXIT-UNDEFINED-DAG: U _Z7observel
// ATEXIT-UNDEFINED-DAG: U __cxa_guard_acquire
// ATEXIT-UNDEFINED-DAG: U __cxa_guard_release
// ATEXIT-UNDEFINED-DAG: U atexit
// ATEXIT-UNDEFINED-NOT: U

// CXA-OBJECT: R_MMIX_GETA _ZN5ValueD1Ev 0x0
// CXA-OBJECT: R_MMIX_GETA __dso_handle 0x0
// CXA-OBJECT: R_MMIX_GETA __cxa_atexit 0x0
// CXA-OBJECT-DAG: Name: _ZN5ValueD1Ev
// CXA-OBJECT-DAG: Name: __cxa_atexit
// CXA-OBJECT-DAG: Name: __dso_handle

// ATEXIT-OBJECT: R_MMIX_PUSHJ_STUBBABLE _ZN5ValueD1Ev 0x0
// ATEXIT-OBJECT: R_MMIX_GETA atexit 0x0
// ATEXIT-OBJECT-DAG: Name: __dtor_global
// ATEXIT-OBJECT-DAG: Name: __dtor__ZZ5localvE5value
// ATEXIT-OBJECT-DAG: Name: atexit
