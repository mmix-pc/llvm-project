// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-llvm -o %t/local.ll %t/local.cpp
// RUN: FileCheck %s --check-prefixes=IR,IR-O0 < %t/local.ll
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-llvm -o %t/local-opt.ll %t/local.cpp
// RUN: FileCheck %s --check-prefixes=IR,IR-O2 < %t/local-opt.ll
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/local.o %t/local.cpp
// RUN: llvm-nm --undefined-only %t/local.o \
// RUN:   | FileCheck %s --check-prefix=UNDEFINED
// RUN: llvm-readobj --sections --symbols --relocations %t/local.o \
// RUN:   | FileCheck %s --check-prefix=OBJECT \
// RUN:   --implicit-check-not=.tdata --implicit-check-not=.tbss
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/runtime-stubs.o \
// RUN:   %t/runtime-stubs.cpp
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/linked %t/local.o \
// RUN:   %t/runtime-stubs.o
// RUN: llvm-nm --undefined-only %t/linked | count 0
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/local-opt.o %t/local.cpp
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/linked-opt %t/local-opt.o \
// RUN:   %t/runtime-stubs.o
// RUN: llvm-nm --undefined-only %t/linked-opt | count 0
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti \
// RUN:   -fexceptions -fcxx-exceptions -O0 -mrelocation-model static \
// RUN:   -emit-llvm -o - %t/local.cpp | FileCheck %s --check-prefix=EH-IR
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti \
// RUN:   -fexceptions -fcxx-exceptions -O0 -mrelocation-model static \
// RUN:   -emit-obj -o %t/local-eh.o %t/local.cpp
// RUN: llvm-readobj --relocations %t/local-eh.o | FileCheck %s --check-prefix=EH-OBJ

//--- local.cpp
extern long make_value(long);

struct Value {
  long value;
  explicit Value(long input) : value(input) {}
};

long guarded() {
  static Value value(make_value(1));
  return value.value;
}

long constant_local() {
  static long value = 7;
  return value;
}

//--- runtime-stubs.cpp
extern "C" int __cxa_guard_acquire(void *) { return 1; }
extern "C" void __cxa_guard_release(void *) {}
long make_value(long value) { return value; }
extern "C" long c_entry() { return 0; }

// IR-O0: @_ZZ7guardedvE5value = internal global %struct.Value zeroinitializer, align 8
// IR-O2: @_ZZ7guardedvE5value.0 = internal unnamed_addr global i64 0, align 8
// IR: @_ZGVZ7guardedvE5value = internal global i64 0, align 8
// IR-O0: @_ZZ14constant_localvE5value = internal global i64 7, align 8
// IR-LABEL: define dso_local noundef i64 @_Z7guardedv()
// IR: load atomic i8, ptr @_ZGVZ7guardedvE5value acquire, align 8
// IR: call i32 @__cxa_guard_acquire(ptr {{.*}}@_ZGVZ7guardedvE5value)
// IR: call noundef i64 @_Z10make_valuel(i64 noundef 1)
// IR-O0: call void @_ZN5ValueC1El({{.*}}@_ZZ7guardedvE5value,
// IR-O2: store i64 %{{.*}}, ptr @_ZZ7guardedvE5value.0, align 8
// IR: call void @__cxa_guard_release(ptr {{.*}}@_ZGVZ7guardedvE5value)
// IR-LABEL: define dso_local noundef i64 @_Z14constant_localv()
// IR-NOT: __cxa_guard
// IR-O0: load i64, ptr @_ZZ14constant_localvE5value, align 8
// IR-O0: ret i64
// IR-O2: ret i64 7

// UNDEFINED-DAG: U _Z10make_valuel
// UNDEFINED-DAG: U __cxa_guard_acquire
// UNDEFINED-DAG: U __cxa_guard_release
// UNDEFINED-NOT: __cxa_guard_abort

// OBJECT: R_MMIX_GETA __cxa_guard_acquire 0x0
// OBJECT: R_MMIX_GETA _Z10make_valuel 0x0
// OBJECT: R_MMIX_GETA __cxa_guard_release 0x0
// OBJECT-DAG: Name: _ZGVZ7guardedvE5value
// OBJECT-DAG: Name: __cxa_guard_acquire
// OBJECT-DAG: Name: __cxa_guard_release

// EH-IR-LABEL: define dso_local noundef i64 @_Z7guardedv()
// EH-IR-SAME: personality ptr @__gxx_personality_v0
// EH-IR: call i32 @__cxa_guard_acquire(ptr @_ZGVZ7guardedvE5value)
// EH-IR: landingpad
// EH-IR: call void @__cxa_guard_abort(ptr @_ZGVZ7guardedvE5value)
// EH-OBJ-DAG: __cxa_guard_abort
// EH-OBJ-DAG: __gxx_personality_v0
