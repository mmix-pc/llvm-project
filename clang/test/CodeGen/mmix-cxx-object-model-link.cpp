// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/model.o %t/model.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/consumer.o %t/consumer.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/entry.o %t/entry.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/runtime.o %t/runtime.cpp
// RUN: ld.lld -m elf64mmix -r -o %t/application.o %t/model.o \
// RUN:   %t/consumer.o %t/entry.o
// RUN: llvm-nm --undefined-only %t/application.o \
// RUN:   | FileCheck %s --check-prefix=DEPENDENCIES
// RUN: llvm-readobj --sections --symbols --relocations %t/application.o \
// RUN:   | FileCheck %s --check-prefix=OBJECT
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/native %t/application.o \
// RUN:   %t/runtime.o
// RUN: llvm-nm --undefined-only %t/native | count 0
// RUN: llvm-nm --defined-only %t/native \
// RUN:   | FileCheck %s --check-prefix=LINKED
// RUN: llvm-ar rc %t/libmodel.a %t/model.o %t/consumer.o
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/archive %t/entry.o \
// RUN:   --whole-archive %t/libmodel.a --no-whole-archive %t/runtime.o
// RUN: llvm-nm --undefined-only %t/archive | count 0
// RUN: llvm-nm --defined-only %t/archive \
// RUN:   | FileCheck %s --check-prefix=LINKED
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/model-opt.o %t/model.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/consumer-opt.o \
// RUN:   %t/consumer.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/entry-opt.o %t/entry.cpp
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/optimized %t/model-opt.o \
// RUN:   %t/consumer-opt.o %t/entry-opt.o %t/runtime.o
// RUN: llvm-nm --undefined-only %t/optimized | count 0
// RUN: llvm-nm --defined-only %t/optimized \
// RUN:   | FileCheck %s --check-prefix=LINKED
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-llvm-bc -o %t/model.bc %t/model.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-llvm-bc -o %t/consumer.bc \
// RUN:   %t/consumer.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-llvm-bc -o %t/entry.bc %t/entry.cpp
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/lto %t/model.bc \
// RUN:   %t/consumer.bc %t/entry.bc %t/runtime.o
// RUN: llvm-nm --undefined-only %t/lto | count 0
// RUN: llvm-nm --defined-only %t/lto \
// RUN:   | FileCheck %s --check-prefix=LTO-LINKED

//--- model.h
extern long make_value(long);

struct Root {
  long root;
  explicit Root(long);
  virtual ~Root();
  virtual long value() const;
};

struct Left : virtual Root {
  long left;
  explicit Left(long);
  ~Left() override;
  long value() const override;
};

struct Right : virtual Root {
  long right;
  explicit Right(long);
  ~Right() override;
  long value() const override;
};

struct Diamond : Left, Right {
  long own;
  explicit Diamond(long);
  ~Diamond() override;
  long value() const override;
};

using RootMethod = long (Root::*)() const;
extern long Root::*root_member;
extern RootMethod root_method;
extern Diamond global_object;
long local_value();
long consume(Diamond *);

//--- model.cpp
#include "model.h"

Root::Root(long input) : root(input) {}
Root::~Root() = default;
long Root::value() const { return root; }
Left::Left(long input) : Root(input), left(input + 1) {}
Left::~Left() = default;
long Left::value() const { return root + left; }
Right::Right(long input) : Root(input), right(input + 2) {}
Right::~Right() = default;
long Right::value() const { return root + right; }
Diamond::Diamond(long input)
    : Root(input), Left(input), Right(input), own(input + 3) {}
Diamond::~Diamond() = default;
long Diamond::value() const { return root + left + right + own; }

long Root::*root_member = &Root::root;
RootMethod root_method = &Root::value;
Diamond global_object(make_value(1));

long local_value() {
  static Diamond object(make_value(2));
  return object.value();
}

//--- consumer.cpp
#include "model.h"

long consume(Diamond *object) {
  Root *root = object;
  Right *right = object;
  return root->value() + right->value() + object->*root_member +
         (root->*root_method)() + local_value();
}

//--- entry.cpp
#include "model.h"

extern "C" long c_entry() { return consume(&global_object); }

//--- runtime.cpp
extern "C" {
char __dso_handle = 0;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
int __cxa_guard_acquire(void *) { return 1; }
void __cxa_guard_release(void *) {}
}

long make_value(long value) { return value; }
using size_t = decltype(sizeof(0));
void operator delete(void *, size_t) noexcept {}

// DEPENDENCIES-DAG: U _Z10make_valuel
// DEPENDENCIES-DAG: U _ZdlPvm
// DEPENDENCIES-DAG: U __cxa_atexit
// DEPENDENCIES-DAG: U __cxa_guard_acquire
// DEPENDENCIES-DAG: U __cxa_guard_release
// DEPENDENCIES-DAG: U __dso_handle
// DEPENDENCIES-NOT: U

// OBJECT: Name: .init_array
// OBJECT: Type: SHT_INIT_ARRAY
// OBJECT-DAG: R_MMIX_64
// OBJECT-DAG: R_MMIX_GETA
// OBJECT-DAG: R_MMIX_GETA
// OBJECT-DAG: Name: _ZTT7Diamond
// OBJECT-DAG: Name: _ZTV7Diamond
// OBJECT-DAG: Name: _ZThn16_NK7Diamond5valueEv
// OBJECT-DAG: Name: _ZGVZ11local_valuevE6object

// LINKED-DAG: T _ZN7DiamondC1El
// LINKED-DAG: T _ZN7DiamondD1Ev
// LINKED-DAG: T _ZNK7Diamond5valueEv
// LINKED-DAG: T _ZThn16_NK7Diamond5valueEv
// LINKED-DAG: R _ZTT7Diamond
// LINKED-DAG: R _ZTV7Diamond
// LINKED-DAG: D root_member
// LINKED-DAG: D root_method
// LINKED-DAG: B global_object
// LINKED-DAG: T c_entry

// LTO-LINKED-DAG: t _ZN7DiamondD1Ev
// LTO-LINKED-DAG: t _ZNK7Diamond5valueEv
// LTO-LINKED-DAG: t _ZThn16_NK7Diamond5valueEv
// LTO-LINKED-DAG: r _ZTV7Diamond
// LTO-LINKED-DAG: b _ZGVZ11local_valuevE6object
// LTO-LINKED-DAG: b global_object
// LTO-LINKED-DAG: T c_entry
